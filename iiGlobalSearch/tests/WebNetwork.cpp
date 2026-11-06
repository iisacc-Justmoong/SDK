#include "WebSearch.h"
#include "../src/Web/WebWait.h"
#include <iiNetwork/Server.h>
#include <iiSDKCore/Runtime/HostLoop.h>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;
int main() try {
    iiGlobalSearch::detail::WebWait wake;
    std::promise<std::uint16_t> portPromise; auto port=portPromise.get_future();
    std::jthread server([&](std::stop_token stop) {
        ii::runtime::EventLoop loop;
        ii::runtime::HostLoop host(loop,[&wake]() noexcept { wake.signal(); });
        ii::network::HttpServer http(loop);
        if(!http.listen("127.0.0.1",0,[](const ii::network::ServerRequest& request) {
            ii::network::ServerResponse response;
            response.headers["Content-Type"]="text/html";
            response.body=request.target=="/robots.txt" ? "User-agent: *\nAllow: /\n" : "<title>Event fixture</title><p>native searchable text</p>";
            return response;
        })) { portPromise.set_value(0); return; }
        portPromise.set_value(http.port());
        while(!stop.stop_requested()) {
            loop.poll();
            if(!stop.stop_requested()) (void)wake.wait_for_loop(loop,stop,std::chrono::steady_clock::time_point::max());
        }
        http.close();
    });
    const auto number=port.get(); if(!number) throw std::runtime_error("fixture listener failed");
    iiGlobalSearch::WebConfiguration config;
    config.defaultSeeds={"http://127.0.0.1:"+std::to_string(number)+"/"};
    iiGlobalSearch::WebSearch search(config);
    iiGlobalSearch::WebRequest request; request.query="searchable"; request.maxPages=1;
    request.maxDepth=0; request.renderJavaScript=false; request.perOriginDelay=0ms; request.timeout=2s;
    auto task=search.submit(request);
    if(!task || !task->waitFor(8s) || task->progress().state!=iiGlobalSearch::WebState::completed || task->results().size()!=1)
        throw std::runtime_error("native HTTP search did not complete");
    server.request_stop(); wake.signal();
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
