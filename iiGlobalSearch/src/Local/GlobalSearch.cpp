#include "GlobalSearch.h"
#include "JsonValue.h"

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
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace iiGlobalSearch {
namespace fs = std::filesystem;
using CoreValue = ii::core::Value;

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
    } else if (auto number = value.get<std::int64_t>()) {
        output += ' '; output += std::to_string(number->get());
    } else if (auto number = value.get<std::uint64_t>()) {
        output += ' '; output += std::to_string(number->get());
    } else if (auto number = value.get<double>()) {
        output += ' '; output += std::to_string(number->get());
    } else if (auto flag = value.get<bool>()) {
        output += flag->get() ? " true" : " false";
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
    const auto parsed = detail::parseCoreJson(document);
    if (!parsed) throw std::runtime_error("Provider classification is invalid JSON");
    const auto* metadata = field(*parsed, "metadata");
    if (!metadata) throw std::runtime_error("Provider classification has no metadata");
    Metadata out{stringField(*metadata, "title"), stringField(*parsed, "fileKey"), {}};
    appendStrings(*metadata, out.terms);
    if (const auto* collections = field(*parsed, "collections")) appendStrings(*collections, out.terms);
    return out;
}

struct ProviderIdentity {
    std::string catalogRelative, contentVersion;
    std::uint64_t revision = 0;
    std::int64_t modifiedAtNs = 0;
    bool package = false, catalogued = false;
};
struct Snapshot {
    iiFileProvider::FileSearchIndex index;
    std::map<std::string, ObjectPresentation, std::less<>> presentations;
    std::map<std::string, FileObject, std::less<>> supplied;
    std::map<std::string, ProviderIdentity, std::less<>> identities;
    std::unordered_set<std::string> packages;
};

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : char(c);
    });
    return value;
}
ObjectType objectType(const fs::path& path, const std::string& mime, const std::string& extension = {}) {
    const auto ext = lowerAscii(extension.empty() ? path.extension().string() : extension);
    const auto contentType = lowerAscii(mime);
    if (ext == ".svg" || contentType.starts_with("image/svg")) return ObjectType::vector;
    if (contentType.starts_with("image/") || ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".webp" || ext == ".gif") return ObjectType::image;
    if (contentType.starts_with("video/") || ext == ".mp4" || ext == ".mov" || ext == ".webm") return ObjectType::video;
    if (contentType.starts_with("audio/") || ext == ".mp3" || ext == ".wav" || ext == ".flac" || ext == ".m4a" || ext == ".ogg") return ObjectType::audio;
    if (ext == ".csv" || ext == ".tsv" || ext == ".xlsx" || ext == ".ods") return ObjectType::spreadsheet;
    if (ext == ".gltf" || ext == ".glb" || ext == ".usd" || ext == ".usdz" || ext == ".obj") return ObjectType::scene;
    if (contentType.starts_with("text/") || contentType == "application/pdf" || ext == ".txt" || ext == ".md" || ext == ".pdf" || ext == ".iisc") return ObjectType::document;
    return ObjectType::other;
}
std::vector<std::string> stringArray(const CoreValue* value) {
    std::vector<std::string> out;
    if (value) if (const auto array = value->get<CoreValue::Array>())
        for (const auto& item : array->get()) if (const auto s = item.get<std::string>()) out.push_back(s->get());
    return out;
}
unsigned ratingValue(const CoreValue* value) {
    if (!value) return 0;
    if (const auto n=value->get<std::uint64_t>()) return n->get() <= 5 ? unsigned(n->get()) : 0;
    if (const auto n=value->get<std::int64_t>()) return n->get() >= 0 && n->get() <= 5 ? unsigned(n->get()) : 0;
    return 0;
}
std::uint64_t nonnegativeInteger(const CoreValue* value) {
    if (!value) return 0;
    if (const auto n=value->get<std::uint64_t>()) return n->get();
    if (const auto n=value->get<std::int64_t>()) return n->get() >= 0 ? std::uint64_t(n->get()) : 0;
    return 0;
}
FileObject fromEntry(const iiFileProvider::FileSearchEntry& entry, const Snapshot& snapshot) {
    if (const auto it=snapshot.supplied.find(entry.path); it!=snapshot.supplied.end()) return it->second;
    FileObject result;
    result.id=entry.path; result.providerId=entry.fileKey; result.relativePath=pathFromText(entry.path);
    result.title=entry.title; result.mimeType=entry.mimeType; result.extension=text(result.relativePath.extension());
    result.type=objectType(result.relativePath,result.mimeType,result.extension); result.size=entry.size;
    result.hasClassification=entry.hasClassification; result.metadataPending=entry.metadataPending;
    if (entry.hasClassification) {
        if (auto document=detail::parseCoreJson(entry.classification)) {
            result.classification=*document;
            result.revision=nonnegativeInteger(field(*document,"revision"));
            if (const auto* metadata=field(*document,"metadata")) {
                result.metadata.additional=*metadata;
                result.metadata.description=stringField(*metadata,"description");
                result.metadata.colorLabel=stringField(*metadata,"color");
                if (result.metadata.colorLabel.empty()) result.metadata.colorLabel="none";
                result.metadata.rating=ratingValue(field(*metadata,"rating"));
                result.metadata.keywords=stringArray(field(*metadata,"keywords"));
                result.metadata.collections=stringArray(field(*metadata,"collections"));
                if (const auto* fields=field(*metadata,"fields")) if (const auto object=fields->get<CoreValue::Object>())
                    for (const auto& [key,value]:object->get()) if (const auto s=value.get<std::string>()) result.metadata.fields.emplace(key,s->get());
            }
        }
    }
    if (const auto it=snapshot.presentations.find(entry.path);it!=snapshot.presentations.end()) {
        result.presentation=it->second;
        result.modifiedAtNs=it->second.modifiedAtNs;
    }
    if (const auto it=snapshot.identities.find(entry.path);it!=snapshot.identities.end()) {
        result.contentVersion=it->second.contentVersion;
        if (it->second.revision) result.revision=it->second.revision;
        result.modifiedAtNs=it->second.modifiedAtNs;
        if (it->second.package) result.type=ObjectType::package;
    }
    return result;
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
    std::shared_ptr<Snapshot> snapshot;
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
    void insert(const Result& entry, const std::string& terms, std::optional<ObjectPresentation> presentation = {},
                std::optional<ProviderIdentity> identity = {}) {
        bool shouldNotify = false;
        {
            std::lock_guard lock(mutex);
            iiFileProvider::FileSearchEntry record;
            record.path = text(entry.relativePath);
            record.title = entry.title;
            record.fileKey = entry.fileKey;
            record.mimeType = entry.mimeType;
            record.classification = entry.classification;
            record.searchTerms = terms + " " + entry.title + " " + entry.fileKey + " " + entry.mimeType;
            if (identity) record.searchTerms += " " + identity->contentVersion;
            record.size = entry.size;
            record.hasClassification = entry.hasClassification;
            record.hasPresentation = entry.hasPresentation;
            record.metadataPending = entry.metadataPending;
            if (snapshot->index.upsert(record)) {
                ++status.indexed;
                shouldNotify = status.indexed == 1 || status.indexed % 64 == 0;
            }
            if (presentation) snapshot->presentations[record.path]=std::move(*presentation);
            if (identity) snapshot->identities[record.path]=std::move(*identity);
        }
        if (shouldNotify) notify();
    }
    bool hasIndex(const fs::path& relative) {
        std::lock_guard lock(mutex);
        return snapshot->index.contains(text(relative));
    }
    void markPackage(const fs::path& relative) {
        std::lock_guard lock(mutex);
        snapshot->packages.insert(text(relative));
    }
    bool hasPackage(const fs::path& relative) {
        std::lock_guard lock(mutex);
        return snapshot->packages.contains(text(relative));
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
                    if (!file) continue;
                    const bool candidate = file.value->kind==iiSocietyContainer::FileTree::Kind::Directory ||
                        lowerAscii(text(key.extension()))==".iiproject";
                    bool package=false;
                    try { package=candidate && catalog.itemKind(row.key)==iiFileProvider::CatalogEntryKind::Package; }
                    catch (const std::exception& e) {
                        if (file.value->kind==iiSocietyContainer::FileTree::Kind::Directory && lowerAscii(text(key.extension()))==".iiproject")
                            markPackage(file.value->relativePath);
                        skipped("Cannot inspect provider kind for " + text(key) + ": " + e.what()); continue;
                    }
                    if (package) markPackage(file.value->relativePath);
                    if (package) {
                        if (file.value->kind!=iiSocietyContainer::FileTree::Kind::Directory) continue;
                        try { if (catalog.resolvePackage(row.key,row.contentVersion)!=file.value->path) continue; }
                        catch (const std::exception& e) { skipped("Invalid provider package for " + text(key) + ": " + e.what()); continue; }
                    } else if (file.value->kind!=iiSocietyContainer::FileTree::Kind::File) continue;
                    try {
                        auto document = catalog.metadataDocument(row.key);
                        auto meta = classification(document);
                        Result result{file.value->relativePath, meta.title, meta.key, {}, std::move(document),
                                      package ? row.size : file.value->size, true, false, row.metadataPending};
                        // The provider's catalog path is normalized independently
                        // of native filesystem spelling (notably APFS NFD names).
                        insert(result, row.path + " " + meta.terms, {}, ProviderIdentity{
                            text(relative), row.contentVersion, row.revision, row.modifiedAtNs, package, true});
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
                std::string terms = record->details.name + " " + record->details.extension + " " + record->details.mimeType +
                    " " + record->sourceVersion + " " + record->preview.kind + " " + record->preview.mimeType;
                for (const auto& [key, value] : record->details.properties) terms += " " + key + " " + value;
                ObjectPresentation details{record->details.name, record->details.extension, record->details.mimeType,
                    record->sourceVersion, record->preview.mimeType, record->preview.kind,
                    record->details.size, record->details.modifiedAtNs, record->details.properties,
                    record->preview.width, record->preview.height, record->embedded};
                insert({file.relativePath, record->details.name, {}, record->details.mimeType, {}, file.size, false, true, false}, terms, std::move(details));
            } else skipped("Provider presentation is stale or invalid: " + text(file.relativePath));
        } else if (ec && ec != std::errc::no_such_file_or_directory) skipped(ec.message());
    }

    bool portablePackage(const iiSocietyContainer::FileTree::Entry& directory) {
        if (lowerAscii(text(directory.path.extension()))!=".iiproject") return false;
        auto companion=directory.path; companion += ".iisacc.json";
        std::error_code ec;
        const auto status=fs::symlink_status(companion,ec);
        if (ec==std::errc::no_such_file_or_directory || (!ec && !fs::exists(status))) return false;
        markPackage(directory.relativePath);
        if (ec) { skipped("Cannot inspect package classification: " + ec.message()); return true; }
        try {
            const auto document=iiFileProvider::FileCatalog::classificationDocument(directory.path);
            if (document.empty()) return true;
            const auto meta=classification(document);
            insert({directory.relativePath, meta.title, meta.key, {}, document, 0, true, false, false}, meta.terms,
                   {}, ProviderIdentity{"", "", 0, 0, true, false});
        } catch (const std::exception& e) { skipped("Invalid package classification for " + text(directory.relativePath) + ": " + e.what()); }
        return true;
    }

    void run() noexcept {
        State end = State::completed;
        try {
            auto next = std::make_shared<Snapshot>();
            std::shared_ptr<Snapshot> retired;
            {
                std::lock_guard lock(mutex);
                retired = std::move(snapshot);
                snapshot = std::move(next);
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
                    if (child.kind == iiSocietyContainer::FileTree::Kind::Directory) {
                        if (!hasPackage(child.relativePath) && !portablePackage(child)) pending.push_back(child.relativePath);
                    }
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

namespace {
constexpr std::size_t sessionMetadataLimit = 256 * 1024;
void checkedText(std::string_view value, std::size_t maximum, std::size_t& bytes) {
    if (value.size() > maximum || value.find('\0') != std::string_view::npos || !ii::core::valid_utf8(value))
        throw std::invalid_argument("Session text is invalid or exceeds its limit");
    if (value.size() > sessionMetadataLimit - bytes) throw std::invalid_argument("Session metadata exceeds its limit");
    bytes += value.size();
}
void checkedValue(const CoreValue& value, std::size_t& bytes, std::size_t& nodes, unsigned depth = 0) {
    if (depth > 32 || ++nodes > 16384) throw std::invalid_argument("Session metadata structure exceeds its limit");
    if (const auto s=value.get<std::string>()) checkedText(s->get(),sessionMetadataLimit,bytes);
    else if (const auto b=value.get<ii::core::Bytes>()) {
        if (b->get().size() > sessionMetadataLimit - bytes) throw std::invalid_argument("Session metadata exceeds its limit");
        bytes += b->get().size();
    } else if (const auto a=value.get<CoreValue::Array>()) {
        if (a->get().size()>4096) throw std::invalid_argument("Session metadata array exceeds its limit");
        for (const auto& item:a->get()) checkedValue(item,bytes,nodes,depth+1);
    } else if (const auto o=value.get<CoreValue::Object>()) {
        if (o->get().size()>4096) throw std::invalid_argument("Session metadata object exceeds its limit");
        for (const auto& [key,item]:o->get()) { checkedText(key,4096,bytes); checkedValue(item,bytes,nodes,depth+1); }
    }
}
void checkedList(const std::vector<std::string>& values, std::size_t& bytes) {
    if (values.size()>128) throw std::invalid_argument("Session metadata list exceeds its limit");
    for (const auto& value:values) checkedText(value,4096,bytes);
}
void validateObject(const FileObject& object) {
    std::size_t bytes=0;
    checkedText(object.id,4096,bytes);
    if (object.id.empty()) throw std::invalid_argument("Session object id is empty");
    for (const auto* value : {&object.providerId,&object.title,&object.mimeType,&object.extension,&object.contentVersion,&object.metadata.colorLabel})
        checkedText(*value,4096,bytes);
    checkedText(text(object.relativePath),4096,bytes);
    checkedText(object.metadata.description,65536,bytes);
    checkedList(object.metadata.keywords,bytes); checkedList(object.metadata.collections,bytes);
    if (object.metadata.fields.size()>64 || object.metadata.rating>5) throw std::invalid_argument("Invalid session classification");
    for (const auto& [key,value]:object.metadata.fields) { checkedText(key,4096,bytes); checkedText(value,65536,bytes); }
    std::size_t nodes=0;
    checkedValue(object.metadata.additional,bytes,nodes); checkedValue(object.classification,bytes,nodes);
    if (object.presentation) {
        const auto& p=*object.presentation;
        for (const auto* value : {&p.name,&p.extension,&p.mimeType,&p.sourceVersion,&p.previewMimeType,&p.previewKind})
            checkedText(*value,4096,bytes);
        if (p.properties.size()>128) throw std::invalid_argument("Too many presentation properties");
        for (const auto& [key,value]:p.properties) { checkedText(key,4096,bytes); checkedText(value,65536,bytes); }
    }
}
void validateFilter(const ObjectFilter& f, std::size_t limit) {
    if (!limit || limit>1024) throw std::invalid_argument("Search limit must be between 1 and 1024");
    if ((f.minimumRating && *f.minimumRating>5) || (f.maximumRating && *f.maximumRating>5) ||
        (f.minimumRating && f.maximumRating && *f.minimumRating>*f.maximumRating) ||
        (f.minimumSize && f.maximumSize && *f.minimumSize>*f.maximumSize) ||
        (f.minimumModifiedAtNs && f.maximumModifiedAtNs && *f.minimumModifiedAtNs>*f.maximumModifiedAtNs))
        throw std::invalid_argument("Invalid search range");
    std::size_t bytes=0;
    for (const auto* value : {&f.text,&f.id,&f.providerId,&f.contentVersion,&f.pathPrefix}) checkedText(*value,4096,bytes);
    for (const auto* values : {&f.mimeTypes,&f.extensions,&f.allKeywords,&f.anyKeywords,&f.collections,&f.colorLabels}) checkedList(*values,bytes);
    if (f.fields.size()>64 || f.presentationFields.size()>128 || f.types.size()>32) throw std::invalid_argument("Too many search filters");
    for (const auto& [key,value]:f.fields) { checkedText(key,4096,bytes); checkedText(value,65536,bytes); }
    for (const auto& [key,value]:f.presentationFields) { checkedText(key,4096,bytes); checkedText(value,65536,bytes); }
}
template<class T> bool contains(const std::vector<T>& values, const T& value) {
    return std::find(values.begin(),values.end(),value)!=values.end();
}
bool anyText(const std::vector<std::string>& requested, const std::vector<std::string>& values) {
    return requested.empty() || std::any_of(requested.begin(),requested.end(),[&](const auto& value) { return contains(values,value); });
}
bool matches(const FileObject& object, const ObjectFilter& filter) {
    if ((!filter.id.empty() && filter.id!=object.id) || (!filter.providerId.empty() && filter.providerId!=object.providerId) ||
        (!filter.contentVersion.empty() && filter.contentVersion!=object.contentVersion) ||
        (filter.revision && *filter.revision!=object.revision)) return false;
    if (!filter.pathPrefix.empty()) {
        const auto path=object.relativePath.empty() ? object.id : text(object.relativePath);
        if (path!=filter.pathPrefix && !path.starts_with(filter.pathPrefix + '/')) return false;
    }
    if (!filter.types.empty() && !contains(filter.types,object.type)) return false;
    const auto inFolded = [](const std::vector<std::string>& choices,const std::string& value) {
        return choices.empty() || std::any_of(choices.begin(),choices.end(),[&](const auto& item) { return lowerAscii(item)==lowerAscii(value); });
    };
    if (!inFolded(filter.mimeTypes,object.mimeType) || !inFolded(filter.extensions,object.extension)) return false;
    if ((filter.minimumRating && object.metadata.rating<*filter.minimumRating) ||
        (filter.maximumRating && object.metadata.rating>*filter.maximumRating) ||
        (filter.minimumSize && object.size<*filter.minimumSize) || (filter.maximumSize && object.size>*filter.maximumSize) ||
        (filter.minimumModifiedAtNs && object.modifiedAtNs<*filter.minimumModifiedAtNs) ||
        (filter.maximumModifiedAtNs && object.modifiedAtNs>*filter.maximumModifiedAtNs)) return false;
    if (!std::all_of(filter.allKeywords.begin(),filter.allKeywords.end(),[&](const auto& word) { return contains(object.metadata.keywords,word); }) ||
        !anyText(filter.anyKeywords,object.metadata.keywords) || !anyText(filter.collections,object.metadata.collections) ||
        !inFolded(filter.colorLabels,object.metadata.colorLabel)) return false;
    for (const auto& [key,value]:filter.fields) {
        const auto found=object.metadata.fields.find(key);
        if (found==object.metadata.fields.end() || found->second!=value) return false;
    }
    for (const auto& [key,value]:filter.presentationFields) {
        if (!object.presentation) return false;
        const auto found=object.presentation->properties.find(key);
        if (found==object.presentation->properties.end() || found->second!=value) return false;
    }
    return (!filter.hasClassification || *filter.hasClassification==object.hasClassification) &&
           (!filter.hasPresentation || *filter.hasPresentation==object.presentation.has_value()) &&
           (!filter.metadataPending || *filter.metadataPending==object.metadataPending);
}
std::unique_ptr<iiSocietyContainer::FileTree> sessionTree(const fs::path& root) {
    if (!iiSocietyContainer::DirectoryStorage::isPackageDirectory(root)) return {};
    auto tree=std::make_unique<iiSocietyContainer::FileTree>(root);
    if (!tree->isValid() || tree->rootPath()!=root) return {};
    return tree;
}
bool visibleObject(const iiSocietyContainer::FileTree& tree,const fs::path& root,
                   const ProviderIdentity* identity,const FileObject& object) {
    const auto entry=tree.entry(object.relativePath);
    if (!entry) return false;
    if (object.type!=ObjectType::package) return entry.value->kind==iiSocietyContainer::FileTree::Kind::File;
    if (!identity || !identity->package || entry.value->kind!=iiSocietyContainer::FileTree::Kind::Directory) return false;
    try {
        if (!identity->catalogued) return !iiFileProvider::FileCatalog::classificationDocument(entry.value->path).empty();
        const auto relative=pathFromText(identity->catalogRelative);
        const auto directory=relative.empty() || relative=="." ? root : root/relative;
        const auto checked=tree.entry(relative);
        if (!checked || checked.value->kind!=iiSocietyContainer::FileTree::Kind::Directory || checked.value->path!=directory) return false;
        iiFileProvider::FileCatalog catalog(directory,iiFileProvider::FileCatalog::Access::ReadOnly);
        return catalog.root()==directory && catalog.resolvePackage(object.providerId,identity->contentVersion)==entry.value->path;
    } catch (...) { return false; }
}
}

class ObjectSession::Impl {
public:
    explicit Impl(std::shared_ptr<Snapshot> source, fs::path container = {})
        : snapshot(std::move(source)), root(std::move(container)), readOnly(!root.empty()) {}
    mutable std::mutex mutex;
    std::shared_ptr<Snapshot> snapshot;
    fs::path root;
    bool readOnly=false;
};

ObjectSession::ObjectSession() : d(std::make_unique<Impl>(std::make_shared<Snapshot>())) {}
ObjectSession::ObjectSession(std::unique_ptr<Impl> impl) : d(std::move(impl)) {}
ObjectSession::~ObjectSession() { destroy(); }
void ObjectSession::destroy() noexcept {
    std::lock_guard lock(d->mutex);
    d->snapshot.reset();
    d->root.clear();
}
bool ObjectSession::destroyed() const noexcept {
    std::lock_guard lock(d->mutex);
    return !d->snapshot;
}
void ObjectSession::store(FileObject object) {
    std::lock_guard lock(d->mutex);
    if (!d->snapshot) throw std::logic_error("Object session is destroyed");
    if (d->readOnly) throw std::logic_error("Container object session is read-only");
    validateObject(object);
    if (object.extension.empty()) object.extension=text((object.relativePath.empty() ? pathFromText(object.id) : object.relativePath).extension());
    if (object.type==ObjectType::unknown) object.type=objectType(object.relativePath,object.mimeType,object.extension);
    std::string terms=object.id+" "+text(object.relativePath)+" "+object.providerId+" "+object.title+" "+object.mimeType+" "+object.extension+" "+object.contentVersion+
        " "+object.metadata.description+" "+object.metadata.colorLabel+" "+std::to_string(object.metadata.rating)+" "+std::to_string(object.size);
    for (const auto& s:object.metadata.keywords) terms+=" "+s;
    for (const auto& s:object.metadata.collections) terms+=" "+s;
    for (const auto& [key,value]:object.metadata.fields) terms+=" "+key+" "+value;
    appendStrings(object.metadata.additional,terms); appendStrings(object.classification,terms);
    if (object.presentation) {
        const auto& p=*object.presentation;
        terms+=" "+p.name+" "+p.extension+" "+p.mimeType+" "+p.sourceVersion+" "+p.previewMimeType+" "+p.previewKind;
        for (const auto& [key,value]:p.properties) terms+=" "+key+" "+value;
    }
    iiFileProvider::FileSearchEntry entry;
    entry.path=object.id; entry.title=object.title; entry.fileKey=object.providerId; entry.mimeType=object.mimeType;
    entry.size=object.size; entry.hasClassification=object.hasClassification;
    entry.hasPresentation=object.presentation.has_value(); entry.metadataPending=object.metadataPending; entry.searchTerms=std::move(terms);
    const auto id=object.id;
    auto [it,inserted]=d->snapshot->supplied.emplace(id,std::move(object));
    if (!inserted) throw std::invalid_argument("Duplicate session object id");
    try {
        if (!d->snapshot->index.upsert(entry)) throw std::logic_error("Session index identity conflict");
    } catch (...) { d->snapshot->supplied.erase(it); throw; }
}
std::optional<FileObject> ObjectSession::find(std::string_view id) const {
    std::lock_guard lock(d->mutex);
    if (!d->snapshot) throw std::logic_error("Object session is destroyed");
    std::size_t bytes=0; checkedText(id,4096,bytes);
    auto entry=d->snapshot->index.find(id);
    if (!entry) return {};
    auto object=fromEntry(*entry,*d->snapshot);
    if (!d->root.empty()) {
        auto tree=sessionTree(d->root);
        const auto identity=d->snapshot->identities.find(object.id);
        if (!tree || !visibleObject(*tree,d->root,identity==d->snapshot->identities.end()?nullptr:&identity->second,object)) return {};
    }
    return object;
}
std::vector<FileObject> ObjectSession::search(const ObjectFilter& filter,std::size_t limit) const {
    std::lock_guard lock(d->mutex);
    if (!d->snapshot) throw std::logic_error("Object session is destroyed");
    validateFilter(filter,limit);
    std::unique_ptr<iiSocietyContainer::FileTree> tree;
    if (!d->root.empty()) { tree=sessionTree(d->root); if (!tree) return {}; }
    std::vector<FileObject> results;
    std::string cursor;
    for (;;) {
        const auto page=d->snapshot->index.searchPage(filter.text,cursor,256);
        if (page.empty()) break;
        for (const auto& entry:page) {
            auto object=fromEntry(entry,*d->snapshot);
            const auto identity=d->snapshot->identities.find(object.id);
            if (matches(object,filter) && (!tree || visibleObject(*tree,d->root,identity==d->snapshot->identities.end()?nullptr:&identity->second,object))) {
                results.push_back(std::move(object));
                if (results.size()==limit) return results;
            }
        }
        cursor=page.back().path;
    }
    return results;
}

std::unique_ptr<ObjectSession> Index::openSession() const {
    std::lock_guard lock(d->mutex);
    if (d->status.state!=State::completed || !d->snapshotReady || !d->snapshot)
        throw std::logic_error("A completed index is required to open an object session");
    return std::unique_ptr<ObjectSession>(new ObjectSession(std::make_unique<ObjectSession::Impl>(d->snapshot,d->root)));
}

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
    const auto finished=[&] { return d->status.state != State::indexing; };
    if(timeout==std::chrono::milliseconds::max()) { d->changed.wait(lock,finished); return true; }
    return d->changed.wait_for(lock, timeout, finished);
}
Progress Index::progress() const { std::lock_guard lock(d->mutex); return d->status; }
std::vector<Result> Index::search(const std::string& query, std::size_t limit) const {
    if (!limit || limit > 1024) throw std::invalid_argument("Search limit must be between 1 and 1024");
    if (query.size() > 4096 || query.find('\0') != std::string::npos)
        throw std::invalid_argument("Search text exceeds its limit or contains NUL");
    std::vector<Result> results;
    std::map<std::string,ProviderIdentity,std::less<>> packages;
    {
        std::lock_guard lock(d->mutex);
        if (!d->snapshotReady) return {};
        for (const auto& item : d->snapshot->index.search(query, limit)) {
            results.push_back({pathFromText(item.path), item.title, item.fileKey, item.mimeType,
                item.classification, item.size, item.hasClassification, item.hasPresentation, item.metadataPending});
            const auto info=d->snapshot->identities.find(item.path);
            if (info!=d->snapshot->identities.end() && info->second.package) packages.emplace(item.path,info->second);
        }
    }
    // Recheck each result before exposing it; no deleted or redirected native
    // paths are returned merely because their earlier catalog row still exists.
    if (!d->validRoot()) return {};
    iiSocietyContainer::FileTree tree(d->root);
    std::erase_if(results, [&](const Result& item) {
        const auto id=text(item.relativePath);
        const auto package=packages.find(id);
        FileObject object; object.id=id; object.relativePath=item.relativePath; object.providerId=item.fileKey;
        object.type=package==packages.end() ? ObjectType::other : ObjectType::package;
        return !visibleObject(tree,d->root,package==packages.end()?nullptr:&package->second,object);
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
    Asset asset; asset.reference = {AssetSource::society, root, entry.relativePath, {}, {}, entry.mimeType};
    switch (objectType(entry.relativePath,entry.mimeType)) {
        case ObjectType::vector: return iiGlobalSearch::Value(Vector{asset});
        case ObjectType::image: return iiGlobalSearch::Value(Image{asset});
        case ObjectType::video: return iiGlobalSearch::Value(Video{asset});
        case ObjectType::audio: return iiGlobalSearch::Value(Audio{asset});
        case ObjectType::spreadsheet: { Spreadsheet sheet; sheet.source=std::move(asset); return iiGlobalSearch::Value(std::move(sheet)); }
        case ObjectType::scene: { Scene scene; scene.source=std::move(asset); return iiGlobalSearch::Value(std::move(scene)); }
        default: return {};
    }
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
            auto parsed = detail::parseCoreJson(item.classification);
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
