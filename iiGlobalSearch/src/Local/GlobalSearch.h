#pragma once

#include "Answer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
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

// Application-facing contract. Another search implementation can be supplied
// without changing its consumer; paths remain relative to its selected scope.
class SearchProvider {
public:
    virtual ~SearchProvider() = default;
    virtual bool start() = 0;
    virtual void cancel() noexcept = 0;
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
private:
    class Impl;
    std::unique_ptr<Impl> d;
};

// The reader is pinned to one canonical package and refuses other sources.
[[nodiscard]] std::shared_ptr<AssetReader> makeLocalAssetReader(const std::filesystem::path& container);

} // namespace iiGlobalSearch
