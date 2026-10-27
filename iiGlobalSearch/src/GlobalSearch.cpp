#include "GlobalSearch.h"

#include <DirectoryStorage.h>
#include <FileCatalog.h>
#include <FileTree.h>
#include <iiFileProviderPresentation.h>
#include <iiJson/Json.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include <sqlite3.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace iiGlobalSearch {
namespace fs = std::filesystem;
using CoreValue = ii::json::Value;

namespace {
std::string text(const fs::path& path) {
    const auto value = path.generic_u8string();
    return {reinterpret_cast<const char*>(value.data()), value.size()};
}
fs::path pathFromText(const std::string& value) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(value.data()), value.size()));
}
bool visibleRelative(const fs::path& path) {
    if (path.empty() || path.is_absolute() || path.has_root_name()) return false;
    for (const auto& part : path) {
        const auto name = text(part);
        if (name.empty() || name == "." || name == ".." || name.starts_with('.')) return false;
    }
    return true;
}
void check(sqlite3* db, int code) {
    if (code != SQLITE_OK && code != SQLITE_ROW && code != SQLITE_DONE)
        throw std::runtime_error(db ? sqlite3_errmsg(db) : "SQLite error");
}
void exec(sqlite3* db, const char* sql) { check(db, sqlite3_exec(db, sql, nullptr, nullptr, nullptr)); }

class Statement {
public:
    Statement(sqlite3* db, const char* sql) : db_(db) { check(db, sqlite3_prepare_v2(db, sql, -1, &stmt_, nullptr)); }
    ~Statement() { sqlite3_finalize(stmt_); }
    void bind(int i, const std::string& value) { check(db_, sqlite3_bind_text(stmt_, i, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT)); }
    void bind(int i, std::int64_t value) { check(db_, sqlite3_bind_int64(stmt_, i, value)); }
    bool row() { const int code = sqlite3_step(stmt_); check(db_, code); return code == SQLITE_ROW; }
    std::string string(int i) const {
        const auto* value = sqlite3_column_text(stmt_, i);
        return value ? std::string(reinterpret_cast<const char*>(value), sqlite3_column_bytes(stmt_, i)) : std::string{};
    }
    std::int64_t number(int i) const { return sqlite3_column_int64(stmt_, i); }
private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

const CoreValue* field(const CoreValue& value, std::string_view key) {
    const auto found = value.find(key);
    return found ? &found->get() : nullptr;
}
std::string stringField(const CoreValue& value, std::string_view key) {
    const auto* item = field(value, key);
    if (!item) return {};
    const auto result = item->get<std::string>();
    return result ? result->get() : std::string{};
}
void appendStrings(const CoreValue& value, std::string& output) {
    if (auto string = value.get<std::string>()) {
        output += ' '; output += string->get();
    } else if (auto array = value.get<CoreValue::Array>()) {
        for (const auto& item : array->get()) appendStrings(item, output);
    } else if (auto object = value.get<CoreValue::Object>()) {
        for (const auto& [key, item] : object->get()) {
            output += ' '; output += key; appendStrings(item, output);
        }
    }
}
struct Metadata { std::string title, key, terms; };
Metadata classification(const std::string& document) {
    const auto parsed = ii::json::parse(document);
    if (!parsed) throw std::runtime_error("Provider classification is invalid JSON");
    const auto* metadata = field(*parsed, "metadata");
    if (!metadata) throw std::runtime_error("Provider classification has no metadata");
    Metadata out{stringField(*metadata, "title"), stringField(*parsed, "fileKey"), {}};
    appendStrings(*metadata, out.terms);
    if (const auto* collections = field(*parsed, "collections")) appendStrings(*collections, out.terms);
    return out;
}
// Each whitespace-separated term is a quoted literal FTS prefix. This keeps
// operators, quotes and punctuation under user text semantics rather than SQL.
std::string matchExpression(const std::string& input) {
    if (input.size() > 4096 || input.find('\0') != std::string::npos)
        throw std::invalid_argument("Search text exceeds its limit or contains NUL");
    std::string result;
    for (std::size_t at = 0; at < input.size();) {
        while (at < input.size() && std::isspace(static_cast<unsigned char>(input[at]))) ++at;
        if (at == input.size()) break;
        std::string token;
        while (at < input.size() && !std::isspace(static_cast<unsigned char>(input[at]))) {
            if (input[at] == '"') token += '"';
            token += input[at++];
        }
        if (!result.empty()) result += " AND ";
        result += '"' + token + "\"*";
    }
    return result;
}
sqlite3* createDatabase() {
    sqlite3* db = nullptr;
    const auto code = sqlite3_open_v2(":memory:", &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (code != SQLITE_OK) {
        const std::string message = db ? sqlite3_errmsg(db) : "Cannot open in-memory SQLite index";
        if (db) sqlite3_close(db);
        throw std::runtime_error(message);
    }
    try {
        exec(db, R"SQL(
CREATE TABLE entries(id INTEGER PRIMARY KEY, path TEXT UNIQUE NOT NULL, title TEXT NOT NULL,
 file_key TEXT NOT NULL, mime TEXT NOT NULL, classification TEXT NOT NULL, size INTEGER NOT NULL,
 classified INTEGER NOT NULL, presented INTEGER NOT NULL, pending INTEGER NOT NULL, terms TEXT NOT NULL);
CREATE VIRTUAL TABLE search_index USING fts5(path, terms, content='entries', content_rowid='id',
 tokenize='unicode61 remove_diacritics 2', prefix='2 3 4');
CREATE TRIGGER entries_insert AFTER INSERT ON entries BEGIN
 INSERT INTO search_index(rowid,path,terms) VALUES(new.id,new.path,new.terms); END;
CREATE TRIGGER entries_delete AFTER DELETE ON entries BEGIN
 INSERT INTO search_index(search_index,rowid,path,terms) VALUES('delete',old.id,old.path,old.terms); END;
CREATE TRIGGER entries_update AFTER UPDATE ON entries BEGIN
 INSERT INTO search_index(search_index,rowid,path,terms) VALUES('delete',old.id,old.path,old.terms);
 INSERT INTO search_index(rowid,path,terms) VALUES(new.id,new.path,new.terms); END;
)SQL");
    } catch (...) { sqlite3_close(db); throw; }
    return db;
}
}

class Index::Impl {
public:
    explicit Impl(fs::path path, Observer callback)
        : root(std::move(path)), observer(std::move(callback)), runtime({1, 8}) {}
    ~Impl() {
        stop.store(true);
        runtime.request_stop(ii::runtime::ShutdownMode::drain);
        (void)runtime.join();
        if (db) sqlite3_close(db);
    }
    fs::path root;
    Observer observer;
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    Progress status;
    sqlite3* db = nullptr;
    bool snapshotReady = false;
    std::unordered_set<std::string> indexed;
    std::atomic<bool> stop{false};
    ii::runtime::Runtime runtime;

    void notify() {
        if (!observer) return;
        Progress copy;
        { std::lock_guard lock(mutex); copy = status; }
        try { observer(copy); } catch (...) { /* Observers do not own the job. */ }
    }
    void skipped(const std::string& message) {
        std::lock_guard lock(mutex);
        ++status.skipped;
        if (status.error.empty()) status.error = message;
    }
    void insert(const Result& entry, const std::string& terms) {
        bool shouldNotify = false;
        {
            std::lock_guard lock(mutex);
            const auto relative = text(entry.relativePath);
            Statement insert(db, R"SQL(
INSERT INTO entries(path,title,file_key,mime,classification,size,classified,presented,pending,terms)
 VALUES(?,?,?,?,?,?,?,?,?,?)
 ON CONFLICT(path) DO UPDATE SET
 title=CASE WHEN entries.classified=1 THEN entries.title WHEN excluded.classified=1 THEN excluded.title ELSE entries.title END,
 file_key=CASE WHEN entries.classified=1 THEN entries.file_key ELSE excluded.file_key END,
 mime=CASE WHEN excluded.mime<>'' THEN excluded.mime ELSE entries.mime END,
 classification=CASE WHEN entries.classified=1 THEN entries.classification ELSE excluded.classification END,
 size=excluded.size, classified=max(entries.classified,excluded.classified),
 presented=max(entries.presented,excluded.presented),
 pending=CASE WHEN entries.classified=1 THEN entries.pending ELSE excluded.pending END,
 terms=entries.terms || ' ' || excluded.terms
)SQL");
            insert.bind(1, relative); insert.bind(2, entry.title); insert.bind(3, entry.fileKey);
            insert.bind(4, entry.mimeType); insert.bind(5, entry.classification);
            insert.bind(6, static_cast<std::int64_t>(std::min(entry.size, std::uint64_t(std::numeric_limits<std::int64_t>::max()))));
            insert.bind(7, std::int64_t(entry.hasClassification)); insert.bind(8, std::int64_t(entry.hasPresentation));
            insert.bind(9, std::int64_t(entry.metadataPending)); insert.bind(10, terms);
            (void)insert.row();
            if (indexed.insert(relative).second) {
                ++status.indexed;
                shouldNotify = status.indexed == 1 || status.indexed % 64 == 0;
            }
        }
        if (shouldNotify) notify();
    }
    bool hasIndex(const fs::path& relative) {
        std::lock_guard lock(mutex);
        return indexed.contains(text(relative));
    }
    bool validRoot() const { return iiSocietyContainer::DirectoryStorage::isPackageDirectory(root); }

    void catalog(const iiSocietyContainer::FileTree& tree, const fs::path& relative) {
        const auto dir = relative.empty() || relative == "." ? root : root / relative;
        const auto verifiedDir = tree.entry(relative);
        if (!verifiedDir || verifiedDir.value->kind != iiSocietyContainer::FileTree::Kind::Directory || verifiedDir.value->path != dir) {
            skipped("Catalog directory is unavailable or redirected: " + text(relative));
            return;
        }
        const auto dbPath = dir / ".iisacc-file-provider" / "catalog.sqlite3";
        std::error_code error;
        const auto dbStatus = fs::symlink_status(dbPath, error);
        if (error == std::errc::no_such_file_or_directory || (!error && !fs::exists(dbStatus))) return;
        if (error || !fs::is_regular_file(dbStatus)) { skipped("Provider catalog is inaccessible or redirected: " + text(relative)); return; }
        try {
            iiFileProvider::FileCatalog catalog(dir, iiFileProvider::FileCatalog::Access::ReadOnly);
            if (catalog.root() != dir) throw std::runtime_error("Provider catalog root is redirected");
            std::optional<iiFileProvider::FileCursor> cursor;
            do {
                if (stop.load()) return;
                const auto currentDir = tree.entry(relative);
                if (!currentDir || currentDir.value->path != dir || !validRoot())
                    throw std::runtime_error("Provider catalog root became unavailable or redirected");
                const auto page = catalog.search({}, cursor, 256);
                for (const auto& row : page.files) {
                    if (stop.load()) return;
                    const auto key = (relative.empty() || relative == "." ? fs::path{} : relative) / pathFromText(row.path);
                    if (!visibleRelative(key)) continue;
                    const auto file = tree.entry(key);
                    if (!file || file.value->kind != iiSocietyContainer::FileTree::Kind::File) continue;
                    try {
                        auto document = catalog.metadataDocument(row.key);
                        auto meta = classification(document);
                        Result result{file.value->relativePath, meta.title, meta.key, {}, std::move(document),
                                      file.value->size, true, false, row.metadataPending};
                        // The provider's catalog path is normalized independently
                        // of native filesystem spelling (notably APFS NFD names).
                        insert(result, row.path + " " + meta.terms);
                    } catch (const std::exception& e) { skipped("Invalid catalog metadata for " + text(key) + ": " + e.what()); }
                }
                cursor = page.next;
            } while (cursor);
        } catch (const std::exception& e) { skipped("Cannot read provider catalog at " + text(relative) + ": " + e.what()); }
    }

    void asset(const iiSocietyContainer::FileTree::Entry& file) {
        const auto hadIndex = hasIndex(file.relativePath);
        if (!hadIndex) {
            auto companion = file.path; companion += ".iisacc.json";
            std::error_code ec;
            const auto companionStatus = fs::symlink_status(companion, ec);
            if (!ec && fs::exists(companionStatus)) {
                try {
                    const auto document = iiFileProvider::FileCatalog::classificationDocument(file.path);
                    if (!document.empty()) {
                        const auto meta = classification(document);
                        insert({file.relativePath, meta.title, meta.key, {}, document, file.size, true, false, false}, meta.terms);
                    }
                } catch (const std::exception& e) { skipped("Invalid classification for " + text(file.relativePath) + ": " + e.what()); }
            } else if (ec && ec != std::errc::no_such_file_or_directory) skipped(ec.message());
        }
        const auto preview = iiFileProvider::native::Presentation::companionPath(file.path);
        std::error_code ec;
        const auto previewStatus = fs::symlink_status(preview, ec);
        if (!ec && fs::exists(previewStatus)) {
            const auto record = iiFileProvider::native::Presentation::load(file.path);
            if (record) {
                std::string terms = record->details.name + " " + record->details.extension + " " + record->details.mimeType;
                for (const auto& [key, value] : record->details.properties) terms += " " + key + " " + value;
                insert({file.relativePath, record->details.name, {}, record->details.mimeType, {}, file.size, false, true, false}, terms);
            } else skipped("Provider presentation is stale or invalid: " + text(file.relativePath));
        } else if (ec && ec != std::errc::no_such_file_or_directory) skipped(ec.message());
    }

    void run() noexcept {
        State end = State::completed;
        try {
            auto* retired = createDatabase();
            {
                std::unordered_set<std::string> oldEntries;
                {
                    std::lock_guard lock(mutex);
                    std::swap(db, retired);
                    indexed.swap(oldEntries);
                    snapshotReady = true;
                }
                if (retired) sqlite3_close(retired);
            }
            if (!validRoot()) throw std::runtime_error("Choose an existing canonical .societycontainer directory package");
            iiSocietyContainer::FileTree tree(root);
            if (!tree.isValid() || tree.rootPath() != root) throw std::runtime_error("Container root is invalid or redirected");
            std::vector<fs::path> pending{fs::path{}};
            while (!pending.empty() && !stop.load()) {
                if (!validRoot()) throw std::runtime_error("Container root became unavailable or redirected");
                auto relative = std::move(pending.back()); pending.pop_back();
                catalog(tree, relative);
                if (stop.load()) break;
                auto children = tree.children(relative, false);
                if (!children) { skipped("Cannot inspect directory " + text(relative) + ": " + children.error.message()); continue; }
                { std::lock_guard lock(mutex); ++status.directories; }
                for (const auto& child : *children.value) {
                    if (stop.load()) break;
                    if (child.kind == iiSocietyContainer::FileTree::Kind::Directory) pending.push_back(child.relativePath);
                    else {
                        { std::lock_guard lock(mutex); ++status.files; }
                        asset(child);
                    }
                }
            }
            if (stop.load()) end = State::cancelled;
        } catch (const std::exception& e) {
            std::lock_guard lock(mutex); status.error = e.what(); end = State::failed;
        } catch (...) {
            std::lock_guard lock(mutex); status.error = "Unknown indexing failure"; end = State::failed;
        }
        { std::lock_guard lock(mutex); status.state = end; }
        changed.notify_all();
        notify();
    }
};

Index::Index(fs::path path, Observer observer) : d(std::make_unique<Impl>(std::move(path), std::move(observer))) {}
Index::~Index() = default;
bool Index::start() {
    std::lock_guard lock(d->mutex);
    if (d->status.state == State::indexing) return false;
    d->snapshotReady = false; d->status = {}; d->status.state = State::indexing; d->stop.store(false);
    const auto submitted = d->runtime.post([impl = d.get()] { impl->run(); }, ii::runtime::Priority::background);
    if (!submitted) { d->status.state = State::failed; d->status.error = "Worker submission failed"; d->changed.notify_all(); }
    return bool(submitted);
}
void Index::cancel() noexcept { d->stop.store(true); }
bool Index::waitFor(std::chrono::milliseconds timeout) const {
    std::unique_lock lock(d->mutex);
    return d->changed.wait_for(lock, timeout, [&] { return d->status.state != State::indexing; });
}
Progress Index::progress() const { std::lock_guard lock(d->mutex); return d->status; }
std::vector<Result> Index::search(const std::string& query, std::size_t limit) const {
    if (!limit || limit > 1024) throw std::invalid_argument("Search limit must be between 1 and 1024");
    const auto match = matchExpression(query);
    std::vector<Result> results;
    {
        std::lock_guard lock(d->mutex);
        if (!d->snapshotReady) return {};
        const char* all = "SELECT path,title,file_key,mime,classification,size,classified,presented,pending FROM entries ORDER BY path LIMIT ?";
        const char* filtered = "SELECT e.path,e.title,e.file_key,e.mime,e.classification,e.size,e.classified,e.presented,e.pending FROM search_index JOIN entries e ON e.id=search_index.rowid WHERE search_index MATCH ? ORDER BY rank,e.path LIMIT ?";
        Statement statement(d->db, match.empty() ? all : filtered);
        int parameter = 1; if (!match.empty()) statement.bind(parameter++, match);
        statement.bind(parameter, static_cast<std::int64_t>(limit));
        while (statement.row()) results.push_back({pathFromText(statement.string(0)), statement.string(1), statement.string(2),
            statement.string(3), statement.string(4), static_cast<std::uint64_t>(statement.number(5)),
            bool(statement.number(6)), bool(statement.number(7)), bool(statement.number(8))});
    }
    // Recheck each result before exposing it; no deleted or redirected native
    // paths are returned merely because their earlier catalog row still exists.
    if (!d->validRoot()) return {};
    iiSocietyContainer::FileTree tree(d->root);
    std::erase_if(results, [&](const Result& item) {
        const auto file = tree.entry(item.relativePath);
        return !file || file.value->kind != iiSocietyContainer::FileTree::Kind::File;
    });
    return results;
}

namespace {
const char* localState(State state) {
    switch (state) {
        case State::idle: return "idle"; case State::indexing: return "indexing";
        case State::completed: return "completed"; case State::cancelled: return "cancelled";
        case State::failed: return "failed";
    }
    return "failed";
}
std::optional<iiGlobalSearch::Value> localAsset(const Result& entry, const fs::path& root) {
    if (root.empty()) return {};
    std::string extension = entry.relativePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    auto mime = entry.mimeType;
    std::transform(mime.begin(), mime.end(), mime.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    Asset asset; asset.reference = {AssetSource::society, root, entry.relativePath, {}, {}, entry.mimeType};
    if (extension == ".svg" || mime.starts_with("image/svg")) return iiGlobalSearch::Value(Vector{asset});
    if (mime.starts_with("image/") || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".webp" || extension == ".gif")
        return iiGlobalSearch::Value(Image{asset});
    if (mime.starts_with("video/") || extension == ".mp4" || extension == ".mov" || extension == ".webm") return iiGlobalSearch::Value(Video{asset});
    if (mime.starts_with("audio/") || extension == ".mp3" || extension == ".wav" || extension == ".flac" || extension == ".m4a" || extension == ".ogg") return iiGlobalSearch::Value(Audio{asset});
    if (extension == ".csv" || extension == ".tsv" || extension == ".xlsx" || extension == ".ods") { Spreadsheet sheet; sheet.source = std::move(asset); return iiGlobalSearch::Value(std::move(sheet)); }
    if (extension == ".gltf" || extension == ".glb" || extension == ".usd" || extension == ".usdz" || extension == ".obj") { Scene scene; scene.source = std::move(asset); return iiGlobalSearch::Value(std::move(scene)); }
    return {};
}
Answer localAnswer(const std::vector<Result>& results, const Progress& status, const std::string& query, const fs::path& root) {
    iiGlobalSearch::Value::List items;
    for (const auto& item : results) {
        iiGlobalSearch::Value::Object fields{{"path", iiGlobalSearch::Value(text(item.relativePath))},
            {"title", iiGlobalSearch::Value(item.title)}, {"fileKey", iiGlobalSearch::Value(item.fileKey)},
            {"mimeType", iiGlobalSearch::Value(item.mimeType)}, {"size", iiGlobalSearch::Value(item.size)},
            {"hasClassification", iiGlobalSearch::Value(item.hasClassification)},
            {"hasPresentation", iiGlobalSearch::Value(item.hasPresentation)}, {"metadataPending", iiGlobalSearch::Value(item.metadataPending)}};
        if (item.hasClassification) {
            auto parsed = ii::json::parse(item.classification);
            if (parsed) fields.emplace("classification", iiGlobalSearch::Value::fromCore(*parsed));
        }
        if (auto asset = localAsset(item, root)) fields.emplace("value", std::move(*asset));
        items.emplace_back(std::move(fields));
    }
    return Answer(iiGlobalSearch::Value::Object{{"source", iiGlobalSearch::Value("society")},
        {"query", iiGlobalSearch::Value(query)}, {"state", iiGlobalSearch::Value(localState(status.state))},
        {"partial", iiGlobalSearch::Value(status.state == State::indexing)},
        {"skipped", iiGlobalSearch::Value(status.skipped)}, {"error", iiGlobalSearch::Value(status.error)},
        {"results", iiGlobalSearch::Value(std::move(items))}});
}
}
Answer SearchProvider::searchAnswer(const std::string& query, std::size_t limit) const {
    auto items = search(query, limit); return localAnswer(items, progress(), query, {});
}
Answer Index::searchAnswer(const std::string& query, std::size_t limit) const {
    auto items = search(query, limit); return localAnswer(items, progress(), query, d->root);
}
} // namespace iiGlobalSearch
