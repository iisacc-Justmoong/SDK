#pragma once

#include "Answer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace iiGlobalSearch {

enum class State { idle, indexing, completed, cancelled, failed };

struct Progress {
    State state = State::idle;
    std::uint64_t directories = 0, files = 0, indexed = 0, skipped = 0;
    std::string error;
};

struct Result {
    std::filesystem::path relativePath;
    std::string title, fileKey, mimeType, classification;
    std::uint64_t size = 0;
    bool hasClassification = false, hasPresentation = false, metadataPending = false;
};

enum class ObjectType { unknown, image, vector, video, audio, spreadsheet, scene, document, package, other };

struct ObjectMetadata {
    std::string description, colorLabel = "none";
    std::vector<std::string> keywords, collections;
    unsigned rating = 0;
    std::map<std::string, std::string> fields;
    ii::core::Value additional;
};

// Provider presentation details are metadata only; preview payload bytes are
// intentionally not copied into a search session.
struct ObjectPresentation {
    std::string name, extension, mimeType, sourceVersion, previewMimeType, previewKind;
    std::uint64_t size = 0;
    std::int64_t modifiedAtNs = 0;
    std::map<std::string, std::string> properties;
    unsigned previewWidth = 0, previewHeight = 0;
    bool embedded = false;
};

struct FileObject {
    // id is unique inside this session. providerId preserves the provider's key.
    std::string id, providerId, title, mimeType, extension, contentVersion;
    std::filesystem::path relativePath;
    ObjectType type = ObjectType::unknown;
    std::uint64_t size = 0;
    std::uint64_t revision = 0;
    std::int64_t modifiedAtNs = 0;
    ObjectMetadata metadata;
    std::optional<ObjectPresentation> presentation;
    ii::core::Value classification;
    bool hasClassification = false, metadataPending = false;
};

struct ObjectFilter {
    std::string text, id, providerId, contentVersion, pathPrefix;
    std::vector<ObjectType> types;
    std::vector<std::string> mimeTypes, extensions, allKeywords, anyKeywords, collections, colorLabels;
    std::map<std::string, std::string> fields, presentationFields;
    std::optional<unsigned> minimumRating, maximumRating;
    std::optional<std::uint64_t> minimumSize, maximumSize;
    std::optional<std::uint64_t> revision;
    std::optional<std::int64_t> minimumModifiedAtNs, maximumModifiedAtNs;
    std::optional<bool> hasClassification, hasPresentation, metadataPending;
};

// Owns a process-local search snapshot. The host may store provider-derived
// values directly; Index::openSession() instead shares a completed read-only
// container snapshot. Call destroy() explicitly when finished. The destructor
// is a fallback; searches and writes after destroy() throw std::logic_error.
class ObjectSession final {
public:
    ObjectSession();
    ~ObjectSession();
    ObjectSession(const ObjectSession&) = delete;
    ObjectSession& operator=(const ObjectSession&) = delete;
    void store(FileObject object);
    [[nodiscard]] std::optional<FileObject> find(std::string_view id) const;
    [[nodiscard]] std::vector<FileObject> search(const ObjectFilter& filter = {}, std::size_t limit = 100) const;
    void destroy() noexcept;
    [[nodiscard]] bool destroyed() const noexcept;
private:
    class Impl;
    explicit ObjectSession(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> d;
    friend class Index;
};

// Application-facing contract. Another search implementation can be supplied
// without changing its consumer; paths remain relative to its selected scope.
class SearchProvider {
public:
    virtual ~SearchProvider() = default;
    virtual bool start() = 0;
    virtual void cancel() noexcept = 0;
    // milliseconds::max() waits for completion without a periodic timeout.
    [[nodiscard]] virtual bool waitFor(std::chrono::milliseconds timeout) const = 0;
    [[nodiscard]] virtual Progress progress() const = 0;
    [[nodiscard]] virtual std::vector<Result> search(const std::string& text = {},
                                                      std::size_t limit = 100) const = 0;
    [[nodiscard]] virtual Answer searchAnswer(const std::string& text = {},
                                              std::size_t limit = 100) const;
};

// One read-only, replaceable search snapshot for one canonical directory package.
// The observer runs on the worker, after progress becomes visible and outside
// internal locks. It must return promptly and must not destroy the Index.
class Index final : public SearchProvider {
public:
    using Observer = std::function<void(const Progress&)>;
    explicit Index(std::filesystem::path container, Observer observer = {});
    ~Index() override;
    Index(const Index&) = delete;
    Index& operator=(const Index&) = delete;

    // False means a run is already active. Invalid roots fail asynchronously.
    bool start() override;
    void cancel() noexcept override;
    [[nodiscard]] bool waitFor(std::chrono::milliseconds timeout) const override;
    [[nodiscard]] Progress progress() const override;
    // Unicode word prefixes joined by AND; empty text lists the current snapshot.
    // Results may be partial until State::completed. Maximum limit is 1024.
    [[nodiscard]] std::vector<Result> search(const std::string& text = {},
                                             std::size_t limit = 100) const override;
    [[nodiscard]] Answer searchAnswer(const std::string& text = {},
                                     std::size_t limit = 100) const override;
    // Requires a completed index; the returned snapshot survives a refresh or
    // destruction of this Index until the caller explicitly destroys it.
    [[nodiscard]] std::unique_ptr<ObjectSession> openSession() const;
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

// The reader is pinned to one canonical package and refuses other sources.
[[nodiscard]] std::shared_ptr<AssetReader> makeLocalAssetReader(const std::filesystem::path& container);

} // namespace iiGlobalSearch
