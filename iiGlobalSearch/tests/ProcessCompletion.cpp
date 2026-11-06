#include "../src/Web/ProcessCompletion.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;
using namespace iiGlobalSearch::detail;
int main(int argc,char** argv) try {
    if(argc==2) {
        if(std::string_view(argv[1])=="exit") return 0;
        if(std::string_view(argv[1])=="hold") { std::this_thread::sleep_for(10s); return 0; }
    }
    auto start=[&](const char* mode) {
        ii::process::Launch launch;
        launch.arguments={std::filesystem::absolute(argv[0]).string(),mode};
        launch.input=ii::process::Input::closed; launch.output=ii::process::Output::discard;
        launch.error=ii::process::ErrorOutput::discard;
        auto child=ii::process::Process::start(launch);
        if(!child) throw std::runtime_error("test child failed to start");
        return std::move(*child);
    };
    {
        auto child=start("exit"); WebWait wake;
        ProcessCompletion monitor(child,wake,5s);
        if(wake.wait({},std::chrono::steady_clock::now()+3s)!=WebWake::signaled || !monitor.exited())
            throw std::runtime_error("process completion did not wake the waiter");
    }
    {
        auto child=start("hold"); WebWait wake;
        const auto before=std::chrono::steady_clock::now();
        { ProcessCompletion monitor(child,wake,5s); }
        if(std::chrono::steady_clock::now()-before>3s) throw std::runtime_error("process cancellation did not join promptly");
    }
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
