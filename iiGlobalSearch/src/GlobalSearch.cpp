#include "GlobalSearch.h"

#include <DirectoryStorage.h>
#include <FileCatalog.h>
#include <FileSearchIndex.h>
#include <FileTree.h>
#include <iiFileProviderPresentation.h>
#include <iiJson/Json.h>
#include <iiSDKCore/Runtime/Runtime.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
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
}

class Index::Impl {
public:
    explicit Impl(fs::path path, Observer callback)
        : root(std::move(path)), observer(std::move(callback)), runtime({1, 8}) {}
    ~Impl() {
        stop.store(true);
        runtime.request_stop(ii::runtime::ShutdownMode::drain);
        (void)runtime.join();
    }
    fs::path root;
    Observer observer;
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    Progress status;
    std::unique_ptr<iiFileProvider::FileSearchIndex> db;
    bool snapshotReady = false;
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
            iiFileProvider::FileSearchEntry record;
            record.path = text(entry.relativePath);
            record.title = entry.title;
            record.fileKey = entry.fileKey;
            record.mimeType = entry.mimeType;
            record.classification = entry.classification;
            record.searchTerms = terms;
            record.size = entry.size;
            record.hasClassification = entry.hasClassification;
            record.hasPresentation = entry.hasPresentation;
            record.metadataPending = entry.metadataPending;
            if (db->upsert(record)) {
                ++status.indexed;
                shouldNotify = status.indexed == 1 || status.indexed % 64 == 0;
            }
        }
        if (shouldNotify) notify();
    }
    bool hasIndex(const fs::path& relative) {
        std::lock_guard lock(mutex);
        return db->contains(text(relative));
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
            auto next = std::make_unique<iiFileProvider::FileSearchIndex>();
            std::unique_ptr<iiFileProvider::FileSearchIndex> retired;
            {
                std::lock_guard lock(mutex);
                retired = std::move(db);
                db = std::move(next);
                snapshotReady = true;
            }
            retired.reset();
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
    if (query.size() > 4096 || query.find('\0') != std::string::npos)
        throw std::invalid_argument("Search text exceeds its limit or contains NUL");
    std::vector<Result> results;
    {
        std::lock_guard lock(d->mutex);
        if (!d->snapshotReady) return {};
        for (const auto& item : d->db->search(query, limit))
            results.push_back({pathFromText(item.path), item.title, item.fileKey, item.mimeType,
                item.classification, item.size, item.hasClassification, item.hasPresentation, item.metadataPending});
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
