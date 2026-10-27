#include "WebSearch.h"
#include "WebCli.h"
#include <iiJson/Json.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace iiGlobalSearch;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Fixture {
    std::mutex mutex;
    std::vector<std::string> requests;
    std::atomic<int> version{1}, slowStarted{0};
    std::atomic<bool> rendererFails{false};
    void record(const std::string& url) { std::lock_guard lock(mutex); requests.push_back(url); }
    bool seen(const std::string& url) { std::lock_guard lock(mutex); return std::find(requests.begin(), requests.end(), url) != requests.end(); }
    std::size_t count(const std::string& url) { std::lock_guard lock(mutex); return std::count(requests.begin(), requests.end(), url); }
};
class Fetcher final : public WebFetcher {
public:
    explicit Fetcher(std::shared_ptr<Fixture> f) : fixture(std::move(f)) {}
    WebResponse fetch(const std::string& url, std::chrono::milliseconds, std::size_t, std::stop_token stop) override {
        fixture->record(url); WebResponse response; response.status = 200; response.contentType = "text/html; charset=utf-8";
        if (url == "https://seed.test/robots.txt") {
            response.contentType = "text/plain";
            response.body = "User-agent: *\nDisallow: /blocked\nAllow: /blocked/open\nDisallow: /wild*end$\n";
        } else if (url == "https://seed.test/") {
            response.body = "<title>Seed</title><meta name='description' content='Useful description'>"
                "<script type='application/ld+json'>{\"name\":\"Fixture\"}</script><p id='dynamic'>waiting</p>"
                "<a href='/child#frag'>child</a><a href='/blocked'>blocked</a><a rel='nofollow' href='/never'>never</a>"
                "<a href='http://127.0.0.1:9/private'>local</a>";
        } else if (url == "https://seed.test/child") {
            response.headers.emplace_back("X-Demo-Tag", "headerquasar");
            response.body = "<title>Nebula child</title><article data-project='quasar-adapter'>nebula child</article><img src='/raw.png'>";
        } else if (url == "https://seed.test/picture.png" || url == "https://seed.test/raw.png") {
            response.contentType = "image/png"; response.body = "PNG-bytes";
        } else if (url == "https://seed.test/fresh") {
            response.body = fixture->version.load() == 1 ? "<p>firstalpha</p>" : "<p>secondbeta</p>";
        } else if (url == "https://seed.test/hidden") {
            response.body = "<meta name='robots' content='noindex'><p>nebula hidden</p><a href='/child'>child</a>";
        } else if (url == "https://seed.test/redirect") {
            response.status = 302; response.headers.emplace_back("Location", "/child");
        } else if (url == "https://seed.test/blocked/open") response.body = "<p>nebula allowed</p>";
        else if (url == "https://seed.test/oversize") response.body = std::string(2048, 'a');
        else if (url == "https://seed.test/encoded%2Fpart?q=x%2Fy") response.body = "<p>encoded value</p>";
        else if (url == "https://seed.test/dynamic-redirect") response.body = "<p>redirecting</p>";
        else if (url == "https://seed.test/slow") {
            fixture->slowStarted = 1;
            while (!stop.stop_requested()) std::this_thread::sleep_for(1ms);
            response.cancelled = true; response.error = "cancelled";
        } else { response.status = 404; response.body = "missing"; }
        return response;
    }
private:
    std::shared_ptr<Fixture> fixture;
};
class Factory final : public WebFetcherFactory {
public:
    explicit Factory(std::shared_ptr<Fixture> f) : fixture(std::move(f)) {}
    std::unique_ptr<WebFetcher> create() override { return std::make_unique<Fetcher>(fixture); }
private:
    std::shared_ptr<Fixture> fixture;
};
class Renderer final : public WebRenderer {
public:
    explicit Renderer(std::shared_ptr<Fixture> f) : fixture(std::move(f)) {}
    std::expected<WebRenderedDocument, std::string> render(const std::string& url, std::chrono::milliseconds,
        std::chrono::milliseconds, std::size_t, std::stop_token) override {
        if (fixture->rendererFails.load()) return std::unexpected("renderer unavailable");
        if (url == "https://seed.test/dynamic-redirect") return WebRenderedDocument{"<p>other</p>", "https://seed.test/child"};
        if (url == "https://seed.test/") return WebRenderedDocument{
            "<html><head><title>Seed</title><meta name='description' content='Useful description'><meta property='og:image' content='/picture.png'>"
            "<script type='application/ld+json'>{\"name\":\"Fixture\"}</script></head><body>"
            "<p id='dynamic'>nebula dynamic payload</p><img src='/picture.png'><video><source src='/clip.mp4'></video>"
            "<audio src='/sound.mp3'></audio><svg><circle r='2'></circle></svg><table><tr><th>name</th><td>value</td></tr></table>"
            "<a href='/sheet.xlsx'>sheet</a><a href='/model.glb'>model</a>"
            "<a href='/child#frag'>child</a>"
            "<a href='/blocked'>blocked</a><a rel='nofollow' href='/never'>never</a>"
            "<a href='http://127.0.0.1:9/private'>local</a></body></html>", url};
        return WebRenderedDocument{"<title>Nebula child</title><p>nebula child</p>", url};
    }
private:
    std::shared_ptr<Fixture> fixture;
};
std::shared_ptr<WebTask> submit(WebSearch& search, WebRequest request) {
    auto task = search.submit(std::move(request));
    check(task && task->waitFor(5s), "web task did not finish");
    return task;
}
int main() try {
    auto fixture = std::make_shared<Fixture>();
    WebConfiguration configuration; configuration.defaultSeeds = {"https://seed.test/"};
    configuration.fetchers = std::make_shared<Factory>(fixture); configuration.renderer = std::make_shared<Renderer>(fixture);
    WebSearch search(configuration); WebSearchProvider& provider = search;
    WebRequest request; request.query = "nebula"; request.maxDepth = 1; request.perOriginDelay = 0ms;
    auto initial = provider.submit(request); check(initial && initial->waitFor(5s), "initial task failed");
    const auto cli = detail::parseWebOptions({"nebula", "--seed", "https://seed.test/child", "--raw", "--no-dom", "--depth", "2"}, "resources/web-seeds.txt");
    check(cli.request.query == "nebula" && cli.request.seeds.size() == 1 && !cli.request.renderJavaScript &&
          !cli.request.includeDom && cli.request.maxDepth == 2 && cli.seedFile == "resources/web-seeds.txt", "CLI option parsing failed");
    bool malformed = false;
    try { (void)detail::parseWebOptions({"query", "--seed"}); } catch (const std::invalid_argument&) { malformed = true; }
    check(malformed, "CLI missing option value was accepted");
    const auto json = ii::json::parse(detail::serializeWebResponse(initial->progress(), initial->results()));
    check(json && json->find("results") && json->find("state"), "CLI response JSON is invalid");
    WebDocument legacy; legacy.headers.emplace_back("X-Legacy", std::string(1, static_cast<char>(0xff)));
    check(bool(ii::json::parse(detail::serializeWebResponse({}, {legacy}))), "non-UTF-8 header broke CLI JSON");
    bool dynamic = false, child = false, metadata = false, dom = false, structured = false;
    for (const auto& page : initial->results()) {
        if (page.url == "https://seed.test/") {
            dynamic = page.rendered && page.text.find("nebula dynamic payload") != std::string::npos;
            metadata = page.description == "Useful description"; structured = !page.structuredData.empty();
            for (const auto& node : page.dom) if (node.name == "p" && !node.attributes.empty()) dom = true;
        }
        if (page.url == "https://seed.test/child") child = true;
    }
    check(dynamic && child && metadata && dom && structured, "dynamic DOM or metadata missing");

    auto answer = initial->answer();
    auto pages = answer.find("results")->get<Value::List>();
    check(pages && !pages->empty() && answer.find("state") && answer.find("source"), "web answer root missing");
    const Value* picture = nullptr;
    bool sheet = false, sheetReference = false, scene = false, video = false, audio = false, graphic = false, graph = false;
    for (const auto& page : *pages) {
        if (*page.find("url")->get<std::string>() != "https://seed.test/") continue;
        graph = page.find("domGraph") && page.find("domGraph")->get<Graph>();
        auto assets = page.find("assets")->get<Value::List>();
        for (const auto& asset : *assets) {
            if (asset.get<Image>()) picture = &asset;
            video |= asset.get<Video>() != nullptr; audio |= asset.get<Audio>() != nullptr;
            if (auto vector = asset.get<Vector>()) graphic |= std::get_if<Asset>(&vector->data) && std::get<Asset>(vector->data).bytes;
            if (auto table = asset.get<Spreadsheet>()) {
                sheet |= table->rows.size() == 1 && table->rows[0].size() == 2;
                sheetReference |= table->source.reference.kind == AssetSource::web;
            }
            if (auto model = asset.get<Scene>()) scene |= model->source.reference.kind == AssetSource::web;
        }
    }
    check(picture && video && audio && graphic && sheet && sheetReference && scene && graph, "typed web results missing");
    check(!picture->get<Image>()->content.bytes && !fixture->seen("https://seed.test/picture.png"), "web image fetched eagerly");
    MediaLoader media(makeWebAssetReader(configuration.fetchers));
    auto load = media.submit(*picture, 1024);
    check(load && load->waitFor(2s) && load->progress().state == LoadState::completed &&
          load->result()->get<Image>()->content.bytes->size() == 9, "web image was not loaded asynchronously");
    Image privateImage; privateImage.content.reference.kind = AssetSource::web;
    privateImage.content.reference.url = "http://127.0.0.1:9/private"; privateImage.content.reference.originatingUrl = "https://seed.test/";
    auto tooSmall = media.submit(*picture, 2);
    check(tooSmall && tooSmall->waitFor(2s) && tooSmall->progress().state == LoadState::failed, "oversized media was accepted");
    Image robotsImage; robotsImage.content.reference.kind = AssetSource::web;
    robotsImage.content.reference.url = "https://seed.test/blocked/image.png";
    robotsImage.content.reference.originatingUrl = "https://seed.test/";
    auto disallowed = media.submit(Value(robotsImage), 1024);
    check(disallowed && disallowed->waitFor(2s) && disallowed->progress().state == LoadState::failed &&
          !fixture->seen(robotsImage.content.reference.url), "robots media restriction was bypassed");
    auto blockedAsset = media.submit(Value(privateImage), 1024);
    check(blockedAsset && blockedAsset->waitFor(2s) && blockedAsset->progress().state == LoadState::failed,
          "private discovered media origin was accepted");

    check(!fixture->seen("https://seed.test/blocked") && !fixture->seen("https://seed.test/never"), "robots or nofollow ignored");
    check(!fixture->seen("http://127.0.0.1:9/robots.txt"), "unrequested private origin was fetched");
    WebRequest limited; limited.query = "nebula"; limited.maxDepth = 0; limited.maxPages = 1; limited.perOriginDelay = 0ms;
    check(submit(search, limited)->progress().truncated, "crawl budget did not report truncation");
    WebRequest credential; credential.seeds = {"https://user:secret@seed.test/"}; credential.renderJavaScript = false;
    auto rejected = submit(search, credential);
    check(rejected->progress().state == WebState::failed && rejected->progress().error.find("secret") == std::string::npos,
          "URL credentials were accepted or leaked");
    WebRequest fresh; fresh.query = "firstalpha"; fresh.seeds = {"https://seed.test/fresh"};
    fresh.maxPages = 1; fresh.renderJavaScript = false; fresh.perOriginDelay = 0ms;
    auto first = submit(search, fresh); check(first->results().size() == 1, "first task failed");
    const auto count = fixture->count("https://seed.test/fresh");
    fixture->version = 2; fresh.query = "secondbeta";
    auto second = submit(search, fresh);
    check(second->results().size() == 1 && fixture->count("https://seed.test/fresh") > count, "new task did not refetch");
    check(first->results().front().text.find("firstalpha") != std::string::npos, "previous job changed");
    WebRequest attr; attr.query = "quasar-adapter"; attr.seeds = {"https://seed.test/child"};
    attr.maxPages = 1; attr.includeDom = false; attr.renderJavaScript = false; attr.perOriginDelay = 0ms;
    auto noDom = submit(search, attr);
    check(noDom->results().size() == 1 && noDom->results().front().dom.empty() &&
          !noDom->results().front().assets.empty() && noDom->results().front().assets.front().get<Image>(),
          "DOM attribute search or media without DOM output failed");
    attr.query = "headerquasar"; check(submit(search, attr)->results().size() == 1, "HTTP header search failed");
    WebRequest hidden; hidden.query = "nebula"; hidden.seeds = {"https://seed.test/hidden"};
    hidden.renderJavaScript = false; hidden.perOriginDelay = 0ms;
    auto noindex = submit(search, hidden);
    check(noindex->results().size() == 1 && noindex->results().front().url == "https://seed.test/child", "noindex follow failed");
    WebRequest links; links.query = "nebula"; links.seeds = {"https://seed.test/redirect", "https://seed.test/blocked/open", "https://seed.test/wildZZend"};
    links.renderJavaScript = false; links.maxDomNodes = 2; links.perOriginDelay = 0ms;
    auto redirected = submit(search, links); bool allowed = false, childRedirect = false, truncated = false;
    for (const auto& page : redirected->results()) {
        allowed |= page.url == "https://seed.test/blocked/open"; childRedirect |= page.url == "https://seed.test/child"; truncated |= page.domTruncated;
    }
    check(allowed && childRedirect && truncated && !fixture->seen("https://seed.test/wildZZend"), "redirect, robots or DOM limit failed");
    WebRequest encoded; encoded.query = "encoded"; encoded.seeds = {"https://seed.test/encoded%2Fpart?q=x%2Fy"};
    encoded.maxPages = 1; encoded.renderJavaScript = false; encoded.perOriginDelay = 0ms;
    check(submit(search, encoded)->results().size() == 1 && fixture->seen(encoded.seeds.front()), "escaped URL path changed semantics");
    WebRequest oversized; oversized.query = "a"; oversized.seeds = {"https://seed.test/oversize"};
    oversized.maxBytes = 1024; oversized.renderJavaScript = false; oversized.perOriginDelay = 0ms;
    auto limitedBytes = submit(search, oversized);
    check(limitedBytes->results().empty() && limitedBytes->progress().skipped > 0, "oversized injected response was accepted");
    WebRequest moving; moving.query = "nebula"; moving.seeds = {"https://seed.test/dynamic-redirect"}; moving.perOriginDelay = 0ms;
    auto nav = submit(search, moving);
    check(nav->results().size() == 1 && nav->results().front().url == "https://seed.test/child", "rendered navigation misattributed");
    fixture->rendererFails = true;
    fresh.query = "secondbeta"; fresh.renderJavaScript = true;
    auto fallback = submit(search, fresh);
    check(fallback->results().size() == 1 && !fallback->results().front().rendered && fallback->progress().skipped > 0,
          "rendering failure did not expose raw fallback");
    WebRequest slow; slow.query = "slow"; slow.seeds = {"https://seed.test/slow"}; slow.renderJavaScript = false; slow.perOriginDelay = 0ms;
    auto cancel = search.submit(slow); check(bool(cancel), "cancellation task rejected");
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!fixture->slowStarted.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(1ms);
    check(fixture->slowStarted.load(), "cancellation request never started"); cancel->cancel();
    check(cancel->waitFor(2s) && cancel->progress().state == WebState::cancelled, "cancellation failed");

    fixture->slowStarted = 0;
    WebConfiguration serial = configuration; serial.workers = 1;
    WebSearch oneWorker(serial);
    auto occupied = oneWorker.submit(slow); check(bool(occupied), "serial task rejected");
    const auto queuedDeadline = std::chrono::steady_clock::now() + 2s;
    while (!fixture->slowStarted.load() && std::chrono::steady_clock::now() < queuedDeadline) std::this_thread::sleep_for(1ms);
    check(fixture->slowStarted.load(), "serial worker did not start");
    auto queued = oneWorker.submit(credential); check(bool(queued), "queued task rejected");
    queued->cancel(); occupied->cancel();
    check(occupied->waitFor(2s) && queued->waitFor(2s) && queued->progress().state == WebState::cancelled,
          "queued cancellation was not preserved");
    std::cout << "iiGlobalSearch mocked web tests passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
