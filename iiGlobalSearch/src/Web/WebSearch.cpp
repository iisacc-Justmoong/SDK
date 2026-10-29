#include "WebSearch.h"
#include "WebHtml.h"
#include "WebRenderer.h"
#include <iiNetwork/Http.h>
#include <iiJson/Json.h>
#include <iiNetwork/Address.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace iiGlobalSearch {
namespace {
using namespace std::chrono_literals;
std::string lower(std::string text) {
    for (auto& c : text) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
    return text;
}
std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.pop_back();
    return value;
}
bool localHost(std::string host) {
    host = lower(host);
    if (host == "localhost" || host.ends_with(".localhost") || host.ends_with(".local")) return true;
    if (host.size() > 2 && host.front() == '[' && host.back() == ']') host = host.substr(1, host.size() - 2);
    auto address = ii::network::IpAddress::parse(host);
    if (!address) return false;
    if (address->is_unspecified() || address->is_loopback() || address->is_link_local() || address->is_multicast()) return true;
    const auto& b = address->bytes();
    const auto private4 = [](unsigned a, unsigned b) {
        return a == 10 || a == 127 || a == 0 || (a == 172 && b >= 16 && b <= 31) ||
               (a == 192 && b == 168) || (a == 169 && b == 254) || (a == 100 && b >= 64 && b <= 127);
    };
    if (address->family() == ii::network::AddressFamily::ipv4) return private4(b[0], b[1]);
    if ((b[0] & 0xfe) == 0xfc) return true;
    bool mapped = true; for (std::size_t i = 0; i < 10; ++i) if (b[i]) mapped = false;
    return mapped && b[10] == 0xff && b[11] == 0xff && private4(b[12], b[13]);
}
bool directive(const std::string& input, const std::string& value) {
    auto text = lower(input);
    for (auto& c : text) if (c == ',' || c == ';' || c == ':') c = ' ';
    std::istringstream words(text); std::string token;
    while (words >> token) if (token == value) return true;
    return false;
}
struct Rule { bool allow; std::string pattern; };
struct Group { std::vector<std::string> agents; std::vector<Rule> rules; };
bool ruleMatches(std::string pattern, const std::string& path) {
    bool end = !pattern.empty() && pattern.back() == '$';
    if (end) pattern.pop_back();
    std::size_t i = 0, j = 0, star = std::string::npos, restart = 0;
    while (j < path.size()) {
        if (i == pattern.size() && !end) return true;
        if (i < pattern.size() && pattern[i] == '*') { star = i++; restart = j; }
        else if (i < pattern.size() && pattern[i] == path[j]) { ++i; ++j; }
        else if (star != std::string::npos) { i = star + 1; j = ++restart; }
        else return false;
    }
    while (i < pattern.size() && pattern[i] == '*') ++i;
    return i == pattern.size();
}
struct Robots {
    bool accessible = true;
    std::vector<Rule> rules;
    bool allows(const std::string& path) const {
        if (!accessible) return false;
        std::size_t best = 0; bool result = true;
        for (const auto& rule : rules) {
            if (rule.pattern.empty() || !ruleMatches(rule.pattern, path)) continue;
            const auto length = std::count_if(rule.pattern.begin(), rule.pattern.end(), [](char c) { return c != '*' && c != '$'; });
            if (static_cast<std::size_t>(length) > best || (static_cast<std::size_t>(length) == best && rule.allow)) {
                best = static_cast<std::size_t>(length); result = rule.allow;
            }
        }
        return result;
    }
};
Robots parseRobots(const std::string& document) {
    std::vector<Group> groups; Group current; bool hasRules = false;
    auto flush = [&] { if (!current.agents.empty()) groups.push_back(std::move(current)); current = {}; hasRules = false; };
    std::istringstream input(document); std::string line;
    while (std::getline(input, line)) {
        if (auto hash = line.find('#'); hash != line.npos) line.resize(hash);
        const auto colon = line.find(':'); if (colon == line.npos) continue;
        auto key = lower(trim(line.substr(0, colon))); auto value = trim(line.substr(colon + 1));
        if (key == "user-agent") {
            if (hasRules) flush();
            if (!value.empty()) current.agents.push_back(lower(value));
        } else if ((key == "allow" || key == "disallow") && !current.agents.empty()) {
            hasRules = true;
            if (!value.empty() && current.rules.size() < 4096 && value.size() <= 8192) current.rules.push_back({key == "allow", value});
        }
    }
    flush();
    std::size_t best = 0;
    for (const auto& group : groups) for (const auto& agent : group.agents)
        if (agent != "*" && std::string("iiglobalsearch").find(agent) != std::string::npos) best = std::max(best, agent.size());
    Robots output;
    for (const auto& group : groups) {
        bool chosen = false;
        for (const auto& agent : group.agents)
            if ((!best && agent == "*") || (best && agent.size() == best && std::string("iiglobalsearch").find(agent) != std::string::npos)) chosen = true;
        if (chosen) output.rules.insert(output.rules.end(), group.rules.begin(), group.rules.end());
    }
    return output;
}
std::optional<std::string> header(const WebResponse& response, const std::string& name) {
    const auto key = lower(name);
    for (const auto& [field, value] : response.headers) if (lower(field) == key) return value;
    return {};
}
class NetworkFetcher final : public WebFetcher {
public:
    WebResponse fetch(const std::string& url, std::chrono::milliseconds timeout,
                      std::size_t maxBytes, std::stop_token stop) override {
        ii::network::HttpRequest request;
        request.url = url; request.timeout = timeout; request.connect_timeout = std::min(timeout, 5000ms);
        request.maximum_bytes = maxBytes; request.follow_redirects = false; request.accept_compression = true;
        request.user_agent = "iiGlobalSearch/0.1 (+https://iisacc.com/)";
        request.headers = {{"Accept", "text/html,application/xhtml+xml,text/plain;q=0.8,*/*;q=0.1"},
                           {"Cache-Control", "no-cache"}};
        bool done = false; ii::network::HttpResponse response;
        if (!http.request(std::move(request), [&](auto result) { response = std::move(result); done = true; })) {
            WebResponse failed; failed.error = "iiNetwork rejected HTTP request"; return failed;
        }
        while (!done && !stop.stop_requested()) { loop.poll(); std::this_thread::sleep_for(2ms); }
        if (!done) { http.cancel(); response.cancelled = true; response.error = "cancelled"; }
        return {response.status, std::move(response.body), std::move(response.error),
                std::move(response.content_type), response.header_fields.entries(), response.cancelled};
    }
private:
    ii::runtime::EventLoop loop;
    ii::network::Http http{loop};
};
std::vector<std::string> words(const std::string& query) {
    std::istringstream input(lower(query)); std::vector<std::string> result; std::string value;
    while (input >> value) result.push_back(value);
    return result;
}
bool rank(WebDocument& page, const std::string& dom, const std::vector<std::string>& terms) {
    const auto title = lower(page.title), description = lower(page.description), body = lower(page.text), url = lower(page.url), domTerms = lower(dom);
    std::string metadata;
    for (const auto& [key, value] : page.metadata) metadata += ' ' + lower(key) + ' ' + lower(value);
    for (const auto& [key, value] : page.headers) metadata += ' ' + lower(key) + ' ' + lower(value);
    for (const auto& value : page.structuredData) metadata += ' ' + lower(value);
    double score = 0;
    auto add = [&](const char* field, const std::string& text, const std::string& term, double weight) {
        if (text.find(term) == text.npos) return false;
        score += weight;
        if (std::find(page.matchedFields.begin(), page.matchedFields.end(), field) == page.matchedFields.end()) page.matchedFields.emplace_back(field);
        return true;
    };
    for (const auto& term : terms) {
        bool found = false;
        found |= add("title", title, term, 8); found |= add("description", description, term, 4);
        found |= add("metadata", metadata, term, 3); found |= add("text", body, term, 1);
        found |= add("dom", domTerms, term, 2); found |= add("url", url, term, 2);
        if (!found) return false;
    }
    page.score = score;
    return true;
}
}

namespace {

const char* webStateName(WebState state) {
    switch (state) {
        case WebState::queued: return "queued"; case WebState::running: return "running";
        case WebState::completed: return "completed"; case WebState::cancelled: return "cancelled";
        case WebState::failed: return "failed";
    }
    return "failed";
}
Value pairValues(const std::vector<std::pair<std::string, std::string>>& entries) {
    Value::List output;
    for (const auto& [name, value] : entries) output.emplace_back(Value::Object{{"name", Value(name)}, {"value", Value(value)}});
    return Value(std::move(output));
}
Value stringValues(const std::vector<std::string>& entries) {
    Value::List output; for (const auto& entry : entries) output.emplace_back(entry); return Value(std::move(output));
}
Answer webAnswer(const WebProgress& status, const std::vector<WebDocument>& pages) {
    Value::List items;
    for (const auto& page : pages) {
        Value::List nodes, structured;
        Graph graph;
        for (std::size_t i = 0; i < page.dom.size(); ++i) {
            const auto& node = page.dom[i];
            nodes.emplace_back(Value::Object{{"parent", Value(node.parent)}, {"type", Value(node.type)},
                {"name", Value(node.name)}, {"text", Value(node.text)}, {"attributes", pairValues(node.attributes)}});
            graph.nodes.push_back({std::to_string(i), node.type == "element" ? node.name : node.text, {}});
            if (node.parent >= 0 && static_cast<std::size_t>(node.parent) < page.dom.size())
                graph.edges.push_back({std::to_string(node.parent), std::to_string(i), "child", {}});
        }
        for (const auto& json : page.structuredData) {
            auto parsed = ii::json::parse(json);
            structured.push_back(parsed ? Value::fromCore(*parsed) : Value(json));
        }
        Value::Object fields{{"url", Value(page.url)}, {"canonicalUrl", Value(page.canonicalUrl)},
            {"title", Value(page.title)}, {"description", Value(page.description)}, {"contentType", Value(page.contentType)},
            {"text", Value(page.text)}, {"html", Value(page.html)}, {"status", Value(static_cast<std::int64_t>(page.status))},
            {"score", Value(page.score)}, {"rendered", Value(page.rendered)}, {"domTruncated", Value(page.domTruncated)},
            {"assetsTruncated", Value(page.assetsTruncated)}, {"metadata", pairValues(page.metadata)}, {"headers", pairValues(page.headers)},
            {"links", stringValues(page.links)}, {"matchedFields", stringValues(page.matchedFields)},
            {"structuredData", Value(std::move(structured))}, {"dom", Value(std::move(nodes))},
            {"assets", Value(page.assets)}};
        if (!page.dom.empty()) fields.emplace("domGraph", Value(std::move(graph)));
        items.emplace_back(std::move(fields));
    }
    return Answer(Value::Object{{"source", Value("web")}, {"state", Value(webStateName(status.state))},
        {"partial", Value(status.state == WebState::queued || status.state == WebState::running)},
        {"fetched", Value(std::uint64_t(status.fetched))}, {"matched", Value(std::uint64_t(status.matched))},
        {"skipped", Value(std::uint64_t(status.skipped))}, {"discovered", Value(std::uint64_t(status.discovered))},
        {"error", Value(status.error)}, {"diagnostics", stringValues(status.diagnostics)},
        {"truncated", Value(status.truncated)}, {"results", Value(std::move(items))}});
}
struct JobState {
    mutable std::mutex mutex;
    mutable std::condition_variable changed;
    std::stop_source stop;
    WebProgress progress;
    std::vector<WebDocument> results;
};
class LiveWebTask final : public WebTask {
public:
    explicit LiveWebTask(std::shared_ptr<JobState> state) : state_(std::move(state)) {}
    void cancel() noexcept override { state_->stop.request_stop(); }
    bool waitFor(std::chrono::milliseconds timeout) const override {
        std::unique_lock lock(state_->mutex);
        return state_->changed.wait_for(lock, timeout, [&] {
            return state_->progress.state == WebState::completed || state_->progress.state == WebState::cancelled || state_->progress.state == WebState::failed;
        });
    }
    WebProgress progress() const override { std::lock_guard lock(state_->mutex); return state_->progress; }
    std::vector<WebDocument> results() const override { std::lock_guard lock(state_->mutex); return state_->results; }
    Answer answer() const override {
        WebProgress status; std::vector<WebDocument> pages;
        { std::lock_guard lock(state_->mutex); status = state_->progress; pages = state_->results; }
        return webAnswer(status, pages);
    }
private:
    std::shared_ptr<JobState> state_;
};
}

Answer WebTask::answer() const { auto pages = results(); return webAnswer(progress(), pages); }

struct WebSearch::Impl {
    explicit Impl(WebConfiguration config)
        : configuration(std::move(config)), renderer(detail::browserRenderer(configuration)),
          runtime({configuration.workers, configuration.capacity}) {}
    ~Impl() {
        { std::lock_guard lock(mutex); for (auto& weak : tasks) if (auto task = weak.lock()) task->stop.request_stop(); }
        runtime.request_stop(ii::runtime::ShutdownMode::drain);
        (void)runtime.join();
    }
    WebConfiguration configuration;
    std::shared_ptr<WebRenderer> renderer;
    ii::runtime::Runtime runtime;
    std::mutex mutex;
    std::vector<std::weak_ptr<JobState>> tasks;

    void run(const std::shared_ptr<JobState>& job, WebRequest request) noexcept {
        const auto stop = job->stop.get_token();
        auto setError = [&](const std::string& error) {
            std::lock_guard lock(job->mutex); ++job->progress.skipped;
            if (job->progress.error.empty()) job->progress.error = error;
            if (job->progress.diagnostics.size() < 32) job->progress.diagnostics.push_back(error.substr(0, 2048));
        };
        WebState final = WebState::completed;
        try {
            { std::lock_guard lock(job->mutex); job->progress.state = WebState::running; }
            if (stop.stop_requested()) {
                { std::lock_guard lock(job->mutex); job->progress.state = WebState::cancelled; }
                job->changed.notify_all(); return;
            }
            if (request.maxPages == 0 || request.maxPages > 1000 || request.maxDepth > 12 || request.maxResults == 0 || request.maxResults > 1000 ||
                request.maxBytes < 1024 || request.maxBytes > 8 * 1024 * 1024 || request.maxDomNodes == 0 || request.maxDomNodes > 100000 ||
                request.timeout < 100ms || request.timeout > 120s || request.renderWait < 0ms || request.renderWait > request.timeout ||
                request.perOriginDelay < 0ms || request.perOriginDelay > 30s || request.query.size() > 4096)
                throw std::invalid_argument("Invalid web search limits");
            if (request.seeds.empty()) request.seeds = configuration.defaultSeeds;
            else if (request.includeDefaultSeeds) request.seeds.insert(request.seeds.end(), configuration.defaultSeeds.begin(), configuration.defaultSeeds.end());
            if (request.seeds.empty() || request.seeds.size() > 1000) throw std::invalid_argument("At least one bounded starting URL is required");
            struct Pending { std::string url; std::size_t depth; };
            std::deque<Pending> queue;
            std::unordered_set<std::string> seen, seedOrigins, canonical;
            const auto queueLimit = std::min<std::size_t>(50000, request.maxPages * 20 + request.seeds.size());
            auto add = [&](const std::string& value, std::size_t depth, bool seed) {
                auto normalized = detail::resolveUrl(value);
                if (!normalized) { if (seed) setError("Invalid starting URL"); return; }
                const auto origin = detail::originOf(*normalized);
                if (seed) seedOrigins.insert(origin);
                if (!seed && request.sameOriginOnly && !seedOrigins.contains(origin)) return;
                if (!seed && !seedOrigins.contains(origin) && localHost(detail::hostOf(*normalized))) return;
                if (seen.size() >= queueLimit) { std::lock_guard lock(job->mutex); job->progress.truncated = true; return; }
                if (!seen.insert(*normalized).second) return;
                queue.push_back({std::move(*normalized), depth});
                std::lock_guard lock(job->mutex); job->progress.discovered = seen.size();
            };
            for (const auto& seed : request.seeds) add(seed, 0, true);
            if (queue.empty()) throw std::invalid_argument("No valid HTTP or HTTPS starting URLs");
            auto network = configuration.fetchers ? configuration.fetchers->create() : std::make_unique<NetworkFetcher>();
            if (!network) throw std::runtime_error("Web fetcher factory returned no fetcher");
            std::unordered_map<std::string, Robots> robots;
            std::unordered_map<std::string, std::chrono::steady_clock::time_point> nextRequest;
            const auto terms = words(request.query);
            while (!queue.empty() && !stop.stop_requested()) {
                { std::lock_guard lock(job->mutex); if (job->progress.fetched >= request.maxPages) { job->progress.truncated = true; break; } }
                auto item = std::move(queue.front()); queue.pop_front();
                const auto origin = detail::originOf(item.url);
                auto found = robots.find(origin);
                if (found == robots.end()) {
                    Robots policy;
                    WebResponse response;
                    std::string robotsUrl = origin + "/robots.txt";
                    for (int redirect = 0; redirect < 6; ++redirect) {
                        response = network->fetch(robotsUrl, request.timeout, std::min<std::size_t>(request.maxBytes, 512 * 1024), stop);
                        if (response.body.size() > std::min<std::size_t>(request.maxBytes, 512 * 1024)) {
                            response.error = "robots response exceeds byte limit"; break;
                        }
                        if (response.cancelled || response.status < 300 || response.status >= 400) break;
                        const auto location = header(response, "location");
                        const auto target = location ? detail::resolveUrl(*location, robotsUrl) : std::nullopt;
                        if (!target || (!seedOrigins.contains(detail::originOf(*target)) && localHost(detail::hostOf(*target)))) break;
                        robotsUrl = *target;
                    }
                    if (response.cancelled) break;
                    if (!response.error.empty() || response.status == 401 || response.status == 403 || response.status == 429 || response.status >= 500 || response.status < 200)
                        policy.accessible = false;
                    else if (response.status >= 300 && response.status < 400) policy.accessible = false;
                    else if (response.status >= 200 && response.status < 300) policy = parseRobots(response.body);
                    found = robots.emplace(origin, std::move(policy)).first;
                }
                if (!found->second.allows(detail::pathOf(item.url))) { setError("robots.txt disallows " + item.url); continue; }
                while (!stop.stop_requested() && std::chrono::steady_clock::now() < nextRequest[origin]) std::this_thread::sleep_for(10ms);
                if (stop.stop_requested()) break;
                nextRequest[origin] = std::chrono::steady_clock::now() + request.perOriginDelay;
                auto response = network->fetch(item.url, request.timeout, request.maxBytes, stop);
                { std::lock_guard lock(job->mutex); ++job->progress.fetched; }
                if (response.cancelled || stop.stop_requested()) break;
                if (!response.error.empty()) { setError("HTTP " + item.url + ": " + response.error); continue; }
                if (response.body.size() > request.maxBytes) { setError("HTTP response exceeds byte limit: " + item.url); continue; }
                if (response.status >= 300 && response.status < 400) {
                    if (const auto location = header(response, "location")) {
                        if (const auto redirected = detail::resolveUrl(*location, item.url)) add(*redirected, item.depth, false);
                    }
                    continue;
                }
                if (!response.ok()) { setError("HTTP " + std::to_string(response.status) + " for " + item.url); continue; }
                const auto type = lower(response.contentType);
                if (!type.empty() && type.find("text/html") == type.npos && type.find("application/xhtml+xml") == type.npos) continue;
                std::string html = std::move(response.body);
                bool rendered = false;
                detail::ParsedPage original;
                try { original = detail::parseHtml(item.url, html, request.maxDomNodes, false); }
                catch (const std::exception& e) { setError("Parsing " + item.url + ": " + e.what()); continue; }
                for (const auto& [key, value] : response.headers) if (lower(key) == "x-robots-tag") {
                    if (directive(value, "noindex") || directive(value, "none")) original.noindex = true;
                    if (directive(value, "nofollow") || directive(value, "none")) original.nofollow = true;
                }
                if (original.noindex) {
                    if (!original.nofollow && item.depth < request.maxDepth)
                        for (const auto& link : original.follow) add(link, item.depth + 1, false);
                    else if (!original.nofollow && !original.follow.empty()) { std::lock_guard lock(job->mutex); job->progress.truncated = true; }
                    continue;
                }
                if (request.renderJavaScript) {
                    auto result = renderer->render(item.url, request.timeout, request.renderWait, request.maxBytes, stop);
                    if (!result || result->finalUrl.empty() || result->html.size() > request.maxBytes) {
                        if (stop.stop_requested()) break;
                        const auto problem = !result ? result.error() : result->finalUrl.empty() ? "renderer did not report the final URL" : "renderer exceeded byte limit";
                        setError("Rendering " + item.url + ": " + problem);
                    } else {
                      if (!result->finalUrl.empty()) {
                        auto finalUrl = detail::resolveUrl(result->finalUrl);
                        if (!finalUrl || *finalUrl != item.url) {
                            if (finalUrl) add(*finalUrl, item.depth, false);
                            setError("Browser navigated away from " + item.url); continue;
                        }
                      }
                      html = std::move(result->html); rendered = true;
                    }
                }
                try {
                    auto parsed = detail::parseHtml(item.url, html, request.maxDomNodes, request.includeDom);
                    auto& document = parsed.document; document.status = response.status; document.contentType = response.contentType; document.rendered = rendered;
                    document.headers = response.headers;
                    parsed.noindex |= original.noindex; parsed.nofollow |= original.nofollow;
                    if (!parsed.nofollow && item.depth < request.maxDepth)
                        for (const auto& link : parsed.follow) { if (stop.stop_requested()) break; add(link, item.depth + 1, false); }
                    else if (!parsed.nofollow && !parsed.follow.empty()) { std::lock_guard lock(job->mutex); job->progress.truncated = true; }
                    if (parsed.noindex || !rank(document, parsed.searchableDom, terms) || !canonical.insert(document.canonicalUrl).second) continue;
                    std::lock_guard lock(job->mutex);
                    ++job->progress.matched;
                    job->results.push_back(std::move(document));
                    std::sort(job->results.begin(), job->results.end(), [](const auto& a, const auto& b) {
                        return a.score != b.score ? a.score > b.score : a.url < b.url;
                    });
                    if (job->results.size() > request.maxResults) { job->results.resize(request.maxResults); job->progress.truncated = true; }
                } catch (const std::exception& e) { setError("Parsing " + item.url + ": " + e.what()); }
            }
            if (stop.stop_requested()) final = WebState::cancelled;
        } catch (const std::exception& e) {
            std::lock_guard lock(job->mutex); job->progress.error = e.what(); final = WebState::failed;
        } catch (...) {
            std::lock_guard lock(job->mutex); job->progress.error = "Unknown web search failure"; final = WebState::failed;
        }
        { std::lock_guard lock(job->mutex); job->progress.state = final; }
        job->changed.notify_all();
    }
};

namespace {
class WebAssetReader final : public AssetReader {
public:
    WebAssetReader(std::shared_ptr<WebFetcherFactory> factory, std::chrono::milliseconds timeout)
        : factory_(std::move(factory)), timeout_(timeout) {}
    std::expected<LoadedBytes, std::string> read(const AssetReference& reference, std::size_t max,
                                                std::stop_token stop) override {
        if (reference.kind != AssetSource::web) return std::unexpected("Not a web asset");
        auto url = detail::resolveUrl(reference.url);
        const auto source = detail::resolveUrl(reference.originatingUrl);
        if (!url || !source) return std::unexpected("Invalid HTTP(S) asset or originating URL");
        const auto sourceOrigin = detail::originOf(*source);
        auto allowedTarget = [&](const std::string& target) {
            return detail::originOf(target) == sourceOrigin || !localHost(detail::hostOf(target));
        };
        if (!allowedTarget(*url)) return std::unexpected("Discovered private asset origin is not permitted");
        auto fetcher = factory_ ? factory_->create() : std::make_unique<NetworkFetcher>();
        if (!fetcher) return std::unexpected("Web fetcher factory returned no fetcher");
        for (int redirect = 0; redirect < 6; ++redirect) {
            if (stop.stop_requested()) return std::unexpected("cancelled");
            const auto origin = detail::originOf(*url);
            auto robotUrl = origin + "/robots.txt";
            WebResponse robot;
            for (int hop = 0; hop < 6; ++hop) {
                robot = fetcher->fetch(robotUrl, timeout_, 512 * 1024, stop);
                if (robot.body.size() > 512 * 1024) return std::unexpected("Robots response exceeds byte limit");
                if (robot.cancelled || stop.stop_requested()) return std::unexpected("cancelled");
                if (robot.status < 300 || robot.status >= 400) break;
                const auto location = header(robot, "location");
                const auto next = location ? detail::resolveUrl(*location, robotUrl) : std::nullopt;
                if (!next || !allowedTarget(*next)) return std::unexpected("Robots redirect is not permitted");
                robotUrl = *next;
            }
            if (!robot.error.empty() || robot.status < 200 || (robot.status >= 300 && robot.status < 400) ||
                robot.status == 401 || robot.status == 403 || robot.status == 429 || robot.status >= 500)
                return std::unexpected("Robots policy unavailable for asset");
            if (robot.status < 300 && !parseRobots(robot.body).allows(detail::pathOf(*url)))
                return std::unexpected("Robots policy disallows asset");
            auto response = fetcher->fetch(*url, timeout_, max, stop);
            if (response.cancelled || stop.stop_requested()) return std::unexpected("cancelled");
            if (!response.error.empty()) return std::unexpected(response.error);
            if (response.body.size() > max) return std::unexpected("Asset exceeds byte limit");
            if (response.status >= 300 && response.status < 400) {
                const auto location = header(response, "location");
                const auto next = location ? detail::resolveUrl(*location, *url) : std::nullopt;
                if (!next || !allowedTarget(*next)) return std::unexpected("Asset redirect is not permitted");
                url = next; continue;
            }
            if (!response.ok()) return std::unexpected("Asset HTTP status " + std::to_string(response.status));
            LoadedBytes loaded; loaded.mimeType = response.contentType;
            loaded.bytes.assign(response.body.begin(), response.body.end()); return loaded;
        }
        return std::unexpected("Asset redirect limit exceeded");
    }
private:
    std::shared_ptr<WebFetcherFactory> factory_;
    std::chrono::milliseconds timeout_;
};
}
std::shared_ptr<AssetReader> makeWebAssetReader(std::shared_ptr<WebFetcherFactory> factory, std::chrono::milliseconds timeout) {
    if (timeout < 100ms || timeout > 120s) throw std::invalid_argument("Invalid web asset timeout");
    return std::make_shared<WebAssetReader>(std::move(factory), timeout);
}
WebSearch::WebSearch(WebConfiguration configuration) {
    if (configuration.workers == 0 || configuration.workers > 16 || configuration.capacity == 0 || configuration.capacity > 1024)
        throw std::invalid_argument("Invalid web worker capacity");
    impl_ = std::make_unique<Impl>(std::move(configuration));
}
WebSearch::~WebSearch() = default;
std::shared_ptr<WebTask> WebSearch::submit(WebRequest request) {
    auto state = std::make_shared<JobState>();
    auto task = std::make_shared<LiveWebTask>(state);
    const auto accepted = impl_->runtime.post([owner = impl_.get(), state, request = std::move(request)]() mutable {
        owner->run(state, std::move(request));
    }, ii::runtime::Priority::background);
    if (!accepted) return {};
    {
        std::lock_guard lock(impl_->mutex);
        std::erase_if(impl_->tasks, [](const auto& weak) {
            const auto previous = weak.lock();
            if (!previous) return true;
            std::lock_guard stateLock(previous->mutex);
            return previous->progress.state == WebState::completed || previous->progress.state == WebState::cancelled || previous->progress.state == WebState::failed;
        });
        impl_->tasks.push_back(state);
    }
    return task;
}
} // namespace iiGlobalSearch
