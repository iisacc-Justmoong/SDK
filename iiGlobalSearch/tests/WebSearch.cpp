#include "WebSearch.h"
#include <iiNetwork/Server.h>
#include <iiSDKCore/Runtime/EventLoop.h>
#include <iiSDKCore/Process/Process.h>
#include <iiJson/Json.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using namespace iiGlobalSearch;
namespace fs = std::filesystem;
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }

class Fixture {
public:
    std::atomic<int> version{1}, blocked{0}, requests{0}, slowEntered{0}, wrongAgent{0};
    std::uint16_t port = 0;
    std::jthread thread;
    Fixture() {
        std::promise<std::uint16_t> ready; auto result = ready.get_future();
        thread = std::jthread([this, ready = std::move(ready)](std::stop_token stop) mutable {
            ii::runtime::EventLoop loop; ii::network::HttpServer server(loop);
            if (!server.listen("127.0.0.1", 0, [this](const ii::network::ServerRequest& request) {
                ++requests;
                const auto agent = request.headers.find("user-agent");
                if (agent == request.headers.end() || agent->second.find("iiGlobalSearch") == std::string::npos) ++wrongAgent;
                ii::network::ServerResponse response; response.headers["Content-Type"] = "text/html; charset=utf-8";
                if (request.target == "/robots.txt") {
                    response.headers["Content-Type"] = "text/plain";
                    response.body = "User-agent: *\nDisallow: /blocked\nAllow: /blocked/open\nDisallow: /wild*end$\n";
                } else if (request.target == "/") {
                    response.body = "<!doctype html><html><head><title>Seed page</title>"
                        "<meta name='description' content='A searchable description'>"
                        "<meta property='og:site_name' content='Fixture'>"
                        "<script type='application/ld+json'>{\"name\":\"Fixture\"}</script></head>"
                        "<body><p id='dynamic'>waiting</p>"
                        "<script>document.getElementById('dynamic').textContent='nebula dynamic payload';</script>"
                        "<a href='/child#fragment'>child</a><a href='/blocked'>blocked</a>"
                        "<a href='/never' rel='nofollow'>no follow</a></body></html>";
                } else if (request.target == "/child") {
                    response.headers["X-Demo-Tag"] = "headerquasar";
                    response.body = "<html><head><title>Nebula child</title><link rel='canonical' href='/child'></head>"
                                    "<body><article data-project='quasar-adapter'>nebula inherited link</article></body></html>";
                } else if (request.target == "/fresh") {
                    response.body = version.load() == 1 ? "<title>fresh</title><p>firstalpha</p>" : "<title>fresh</title><p>secondbeta</p>";
                } else if (request.target == "/hidden") {
                    response.body = "<meta name='robots' content='noindex'><p>nebula hidden</p><a href='/child'>child</a>";
                } else if (request.target == "/redirect") {
                    response.status = 302; response.headers["Location"] = "/child";
                } else if (request.target == "/dynamic-redirect") {
                    response.body = "<html><head><script>location.href='/child'</script></head><body>redirecting</body></html>";
                } else if (request.target == "/blocked/open") {
                    response.body = "<title>Allowed</title><p>nebula allowed exception</p>";
                } else if (request.target == "/wildZZend") {
                    ++blocked; response.body = "nebula wildcard blocked";
                } else if (request.target == "/slow") {
                    ++slowEntered; std::this_thread::sleep_for(3s); response.body = "<p>slow response</p>";
                } else if (request.target == "/blocked") { ++blocked; response.body = "nebula blocked"; }
                else if (request.target == "/never") { ++blocked; response.body = "nebula never"; }
                else { response.status = 404; response.body = "missing"; }
                return response;
            })) { ready.set_value(0); return; }
            ready.set_value(server.port());
            while (!stop.stop_requested()) { loop.poll(); std::this_thread::sleep_for(1ms); }
            server.close();
        });
        port = result.get(); check(port != 0, "fixture HTTP server failed");
    }
    std::string url(const char* path) const { return "http://127.0.0.1:" + std::to_string(port) + path; }
};

int main(int argc, char** argv) try {
    check(argc == 3, "resources path and CLI required");
    Fixture fixture;
    const auto scratch = fs::absolute(argv[1]) / "web-rendering";
    const bool scratchCreated = fs::create_directories(scratch);
    struct ScratchCleanup {
        fs::path path; bool created;
        ~ScratchCleanup() { if (created) { std::error_code error; fs::remove(path, error); } }
    } scratchCleanup{scratch, scratchCreated};
    WebConfiguration config;
    config.defaultSeeds = {fixture.url("/")};
    config.browserExecutable = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome";
    config.scratchDirectory = scratch;
    WebSearch search(config);
    WebSearchProvider& provider = search;
    WebRequest query; query.query = "nebula"; query.maxPages = 8; query.maxDepth = 1; query.renderWait = 1200ms; query.timeout = 30s;
    const auto before = std::chrono::steady_clock::now();
    auto task = provider.submit(query);
    check(bool(task), "web job rejected");
    check(std::chrono::steady_clock::now() - before < 1s, "web submission blocked");
    check(task->waitFor(70s), "web job timed out");
    check(task->progress().state == WebState::completed, "web job failed");
    auto pages = task->results();
    bool dynamic = false, child = false, metadata = false, dom = false, structured = false;
    for (const auto& page : pages) {
        if (page.url == fixture.url("/")) {
            dynamic = page.rendered && page.text.find("nebula dynamic payload") != std::string::npos;
            metadata = page.description == "A searchable description";
            structured = !page.structuredData.empty();
            for (const auto& node : page.dom) if (node.name == "p" && !node.attributes.empty()) dom = true;
        }
        if (page.url == fixture.url("/child")) child = true;
    }
    if (!(dynamic && child && metadata && dom && structured)) {
        const auto status = task->progress();
        std::cerr << "fetched=" << status.fetched << " matched=" << status.matched << " skipped=" << status.skipped
                  << " error=" << status.error << " flags=" << dynamic << child << metadata << dom << structured << '\n';
        for (const auto& page : pages) std::cerr << "page=" << page.url << " rendered=" << page.rendered << " title=" << page.title << '\n';
    }
    check(dynamic && child && metadata && dom && structured, "rendered DOM, metadata or followed link missing");
    check(fixture.blocked.load() == 0, "robots or nofollow link was fetched");
    check(fixture.wrongAgent.load() == 0, "network and browser user-agent differ");

    WebRequest fresh; fresh.query = "firstalpha"; fresh.seeds = {fixture.url("/fresh")};
    fresh.maxPages = 1; fresh.renderJavaScript = false;
    auto first = search.submit(fresh); check(first && first->waitFor(15s) && first->results().size() == 1, "first fresh task failed");
    const auto count = fixture.requests.load(); fixture.version.store(2); fresh.query = "secondbeta";
    auto second = search.submit(fresh); check(second && second->waitFor(15s) && second->results().size() == 1, "second fresh task used stale data");
    check(fixture.requests.load() > count, "second task did not request network again");
    check(first->results().front().text.find("firstalpha") != std::string::npos, "earlier job result was replaced");

    WebRequest attributes; attributes.query = "quasar-adapter"; attributes.seeds = {fixture.url("/child")};
    attributes.renderJavaScript = false; attributes.includeDom = false; attributes.maxPages = 1;
    auto byAttribute = search.submit(attributes);
    check(byAttribute && byAttribute->waitFor(15s) && byAttribute->results().size() == 1 && byAttribute->results().front().dom.empty(),
          "DOM attributes are not searchable without DOM output");
    attributes.query = "headerquasar";
    auto byHeader = search.submit(attributes);
    check(byHeader && byHeader->waitFor(15s) && byHeader->results().size() == 1, "HTTP metadata is not searchable");

    WebRequest hidden; hidden.query = "nebula"; hidden.seeds = {fixture.url("/hidden")}; hidden.renderJavaScript = false;
    auto noindex = search.submit(hidden); check(noindex && noindex->waitFor(15s), "noindex task timed out");
    check(noindex->results().size() == 1 && noindex->results().front().url == fixture.url("/child"), "noindex or follow behavior is incorrect");
    WebRequest redirects; redirects.query = "nebula"; redirects.seeds = {fixture.url("/redirect"), fixture.url("/blocked/open"), fixture.url("/wildZZend")};
    redirects.renderJavaScript = false; redirects.maxDomNodes = 2;
    auto httpRedirect = search.submit(redirects); check(httpRedirect && httpRedirect->waitFor(15s), "redirect task timed out");
    bool exceptionFound = false, redirectedFound = false, truncated = false;
    for (const auto& page : httpRedirect->results()) {
        exceptionFound |= page.url == fixture.url("/blocked/open");
        redirectedFound |= page.url == fixture.url("/child"); truncated |= page.domTruncated;
    }
    check(exceptionFound && redirectedFound && truncated && fixture.blocked.load() == 0, "robots allow, wildcard, HTTP redirect or DOM limit failed");

    WebRequest moving; moving.query = "nebula"; moving.seeds = {fixture.url("/dynamic-redirect")};
    moving.maxPages = 3; moving.timeout = 30s; moving.renderWait = 200ms;
    auto jsRedirect = search.submit(moving); check(jsRedirect && jsRedirect->waitFor(75s), "JavaScript redirect task timed out");
    bool finalFound = false, wrongOrigin = false;
    for (const auto& page : jsRedirect->results()) {
        finalFound |= page.url == fixture.url("/child") && page.rendered;
        wrongOrigin |= page.url == fixture.url("/dynamic-redirect");
    }
    check(finalFound && !wrongOrigin, "rendered navigation was misattributed or not followed");

    ii::process::Launch cli;
    cli.arguments = {argv[2], "web", "secondbeta", "--seed", fixture.url("/fresh"), "--max-pages", "1", "--depth", "0", "--raw", "--no-dom"};
    cli.environment["IIGLOBALSEARCH_SEED_FILE"] = std::nullopt;
    cli.input = ii::process::Input::closed;
    auto process = ii::process::Process::start(cli); check(bool(process), "CLI process failed to start");
    ii::process::CommunicationOptions options; options.deadline = ii::core::Deadline::after(25s);
    auto output = process->communicate({}, options);
    check(output && output->exit.reason == ii::process::ExitReason::exited && output->exit.code == 0, "CLI web command failed");
    std::string json(reinterpret_cast<const char*>(output->output.data()), output->output.size());
    const auto parsed = ii::json::parse(json); check(bool(parsed), "CLI did not return JSON");
    const auto entries = parsed->find("results"); check(bool(entries), "CLI results missing");
    const auto list = entries->get().get<ii::json::Value::Array>();
    check(list && list->get().size() == 1 && json.find("secondbeta") != std::string::npos, "CLI returned incorrect web search result");

    WebRequest slow; slow.query = "slow"; slow.seeds = {fixture.url("/slow")}; slow.renderJavaScript = false;
    auto cancelled = search.submit(slow); check(bool(cancelled), "cancel task rejected");
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (!fixture.slowEntered.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(5ms);
    check(fixture.slowEntered.load() > 0, "slow request did not start");
    cancelled->cancel();
    check(cancelled->waitFor(2s) && cancelled->progress().state == WebState::cancelled, "active HTTP cancellation failed");
    std::cout << "iiGlobalSearch live web tests passed\n";
    return 0;
} catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
