#include <iiGPU/Completion.h>
#include "../example/Acceleration/Kernel.h"
#include <array>
#include <iostream>
#include <thread>

using namespace ii::gpu;
using example::take;
int main() {
    try {
        auto result=Device::create();
        if(!result&&(result.error().code==ErrorCode::unsupported||result.error().code==ErrorCode::unavailable)) return 77;
        auto device=take(std::move(result));
        auto buffer=take(device.buffer({1024}));
        auto commands=device.commands(); take(commands.fill(buffer,37)); take(commands.finish());
        auto job=take(device.submit(std::move(commands)));
        ii::runtime::EventLoop loop(4);
        unsigned calls=0; const auto thread=std::this_thread::get_id();
        auto watch=take(CompletionWatch::create(loop,job,[&](Result<void> completed) {
            take(std::move(completed));
            if(std::this_thread::get_id()!=thread) throw std::runtime_error("completion used another thread");
            ++calls;
        }));
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(watch.active()&&std::chrono::steady_clock::now()<deadline) {
            loop.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        if(calls!=1||watch.active()) throw std::runtime_error("completion was not delivered exactly once");
        for(int i=0;i<3;++i) loop.poll();
        if(calls!=1) throw std::runtime_error("completion repeated");
        auto cancelled=take(CompletionWatch::create(loop,job,[&](Result<void>){++calls;}));
        cancelled.cancel(); loop.poll();
        if(cancelled.active()||calls!=1) throw std::runtime_error("cancelled observation ran");
        ii::runtime::EventLoop full(1);
        auto occupied=full.after(std::chrono::hours(1),[]{});
        auto rejected=CompletionWatch::create(full,job,[](Result<void>){});
        if(rejected||rejected.error().code!=ErrorCode::limit_exceeded) throw std::runtime_error("loop capacity not enforced");
        occupied.token.cancel();
        std::cout<<"Core completion exactly_once=true dispatch_thread=true bounded=true\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
