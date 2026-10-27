#pragma once

#include "Answer.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <expected>
#include <map>
#include <memory>
#include <string>
#include <stop_token>
#include <utility>
#include <vector>

namespace iiGlobalSearch {

struct WebNode {
    std::int64_t parent = -1;
    std::string type, name, text;
    std::vector<std::pair<std::string, std::string>> attributes;
};
struct WebDocument {
    std::string url, canonicalUrl, title, description, contentType, text, html;
    std::vector<std::pair<std::string, std::string>> metadata, headers;
    std::vector<std::string> links, structuredData, matchedFields;
    std::vector<WebNode> dom;
    std::vector<Value> assets; // Media references and tables discovered in the parsed DOM.
    long status = 0;
    double score = 0;
    bool rendered = false, domTruncated = false, assetsTruncated = false;
};
struct WebRequest {
    std::string query;
    std::vector<std::string> seeds; // Empty uses the configured defaults.
    bool includeDefaultSeeds = false;
    std::size_t maxPages = 20, maxDepth = 2, maxResults = 20;
    std::size_t maxBytes = 2 * 1024 * 1024, maxDomNodes = 30000;
    std::chrono::milliseconds timeout{20000}, renderWait{1000}, perOriginDelay{100};
    bool renderJavaScript = true, includeDom = true, sameOriginOnly = false;
};
struct WebRenderedDocument { std::string html, finalUrl; };
class WebRenderer {
public:
    virtual ~WebRenderer() = default;
    [[nodiscard]] virtual std::expected<WebRenderedDocument, std::string> render(
        const std::string& url, std::chrono::milliseconds timeout,
        std::chrono::milliseconds settle, std::size_t maxBytes, std::stop_token stop) = 0;
};
struct WebResponse {
    long status = 0;
    std::string body, error, contentType;
    std::vector<std::pair<std::string, std::string>> headers;
    bool cancelled = false;
    [[nodiscard]] bool ok() const { return error.empty() && status >= 200 && status < 300; }
};
class WebFetcher {
public:
    virtual ~WebFetcher() = default;
    [[nodiscard]] virtual WebResponse fetch(const std::string& url, std::chrono::milliseconds timeout,
                                             std::size_t maxBytes, std::stop_token stop) = 0;
};
class WebFetcherFactory {
public:
    virtual ~WebFetcherFactory() = default;
    [[nodiscard]] virtual std::unique_ptr<WebFetcher> create() = 0;
};
struct WebConfiguration {
    std::vector<std::string> defaultSeeds;
    std::filesystem::path browserExecutable, scratchDirectory;
    std::shared_ptr<WebRenderer> renderer; // Optional replaceable browser implementation.
    std::shared_ptr<WebFetcherFactory> fetchers; // Each task receives its own fetcher.
    std::size_t workers = 2, capacity = 16;
};
enum class WebState { queued, running, completed, cancelled, failed };
struct WebProgress {
    WebState state = WebState::queued;
    std::size_t fetched = 0, matched = 0, skipped = 0, discovered = 0;
    std::string error;
    std::vector<std::string> diagnostics; // First 32 skipped operations.
    bool truncated = false; // A depth, page, frontier or result budget was hit.
};

class WebTask {
public:
    virtual ~WebTask() = default;
    virtual void cancel() noexcept = 0;
    [[nodiscard]] virtual bool waitFor(std::chrono::milliseconds timeout) const = 0;
    [[nodiscard]] virtual WebProgress progress() const = 0;
    [[nodiscard]] virtual std::vector<WebDocument> results() const = 0;
    [[nodiscard]] virtual Answer answer() const;
};

// Creates one fresh fetcher per load, honoring robots, URL and byte limits.
[[nodiscard]] std::shared_ptr<AssetReader> makeWebAssetReader(
    std::shared_ptr<WebFetcherFactory> fetchers = {}, std::chrono::milliseconds timeout = std::chrono::seconds(20));

class WebSearchProvider {
public:
    virtual ~WebSearchProvider() = default;
    [[nodiscard]] virtual std::shared_ptr<WebTask> submit(WebRequest request) = 0;
};
// Owns a bounded worker pool. Every submitted task has its own frontier,
// visited URLs, robots policy and results; no persistent or external index.
class WebSearch final : public WebSearchProvider {
public:
    explicit WebSearch(WebConfiguration configuration = {});
    ~WebSearch() override;
    WebSearch(const WebSearch&) = delete;
    WebSearch& operator=(const WebSearch&) = delete;
    [[nodiscard]] std::shared_ptr<WebTask> submit(WebRequest request) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace iiGlobalSearch
