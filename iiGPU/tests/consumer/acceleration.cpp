#include <iiGPU/CompletionService.h>
#include <iiGPU/DeviceThread.h>
#include <iiGPU/Session.h>
#include <iiSDKCore/Runtime/EventLoop.h>
#include <iiSDKCore/Runtime/Runtime.h>
#include "Kernel.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <numeric>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;
namespace {
int contract() {
    std::mutex mutex;
    std::condition_variable changed;
    bool ready=false;std::size_t cpu_started=0;
    std::array<std::thread::id,2> cpu_ids{};std::thread::id gpu_id;
    std::atomic<bool> delivered=false,failed=false;
    ii::runtime::Runtime cpu({.workers=2,.capacity=8,.queues=2});
    auto gpu=cpu.objects().emplace<ii::gpu::DeviceThread>(ii::gpu::DeviceThreadOptions{.capacity=4});
    if(!gpu) return 1;
    ii::runtime::EventLoop host(8);
    const auto sender=host.sender();
    ii::gpu::CompletionService completions({.workers=1,.capacity=2});
    auto ticket=completions.observe([&]() -> ii::gpu::Result<void> {
        std::unique_lock lock(mutex);
        if(!changed.wait_for(lock,5s,[&]{return ready;}))
            return std::unexpected(ii::gpu::Error{ii::gpu::ErrorCode::timeout,"test wait expired"});
        return {};
    },[&](ii::gpu::Result<void> result) {
        const auto posted=sender.post([&,result=std::move(result)] { delivered=bool(result); host.request_stop(); });
        if(!posted) { failed=true; host.request_stop(); }
    });
    std::array<ii::runtime::Submission,2> workers;
    for(std::size_t i=0;i<workers.size();++i)workers[i]=cpu.post([&,i] {
        std::unique_lock lock(mutex);cpu_ids[i]=std::this_thread::get_id();++cpu_started;changed.notify_all();
        if(!changed.wait_for(lock,5s,[&]{return ready;}))failed=true;
    });
    ii::gpu::Result<void> gpu_post;
    const auto access=gpu->with([&](ii::gpu::DeviceThread& thread){gpu_post=thread.post([&]{
        std::unique_lock lock(mutex);
        if(!changed.wait_for(lock,5s,[&]{return cpu_started==workers.size();}))failed=true;
        gpu_id=std::this_thread::get_id();ready=true;changed.notify_all();
    });});
    const auto timeout=host.after(5s,[&]{failed=true; host.request_stop();});
    if(!ticket || !workers[0] || !workers[1] || !gpu_post || access!=ii::runtime::SubmitError::none || !timeout) {
        {std::lock_guard lock(mutex); ready=true;}
        changed.notify_all();
        (void)completions.close();
        (void)cpu.join();
        return 1;
    }
    host.run();
    const auto stopped=completions.close();
    const auto joined=cpu.join();
    return stopped && joined && delivered && !failed && !ticket->pending() && gpu->expired() &&
           gpu_id!=std::thread::id{} && gpu_id!=cpu_ids[0] && gpu_id!=cpu_ids[1] ? 0 : 2;
}
int hardware(ii::gpu::Backend backend) {
    using namespace ii::gpu;
    using example::take;
    std::array<std::uint32_t,257> input{},output{};
    std::iota(input.begin(),input.end(),0u);
    std::atomic<unsigned> cpu_completed=0;
    std::atomic<bool> failed=false,unavailable=false;
    bool gpu_completed=false;
    std::string adapter_name,api_name;
    ii::runtime::Runtime cpu({.capacity=8,.worker_policy=ii::runtime::RuntimeWorkerPolicy::hardware});
    auto gpu=cpu.objects().emplace<DeviceThread>(DeviceThreadOptions{.capacity=4});
    if(!gpu)return 3;
    ii::runtime::EventLoop host(8);
    const auto sender=host.sender();
    CompletionService completions({.workers=1,.capacity=4});
    const auto timeout=host.after(10s,[&]{failed=true;host.request_stop();});
    Result<void> submitted;
    const auto access=gpu->with([&](DeviceThread& thread){submitted=thread.post([&] {
        try {
            Options options;options.backend=backend;auto selected=Device::create(options);
            if(!selected&&(selected.error().code==ErrorCode::unsupported||selected.error().code==ErrorCode::unavailable)){
                unavailable=true;host.request_stop();return;
            }
            auto device=take(std::move(selected));adapter_name=device.adapter().name;api_name=backend_name(device.adapter().backend);
            auto pipeline=take(device.compute_pipeline(example::transform()));
            auto source=take(device.buffer({sizeof(input)}));auto destination=take(device.buffer({sizeof(output)}));
            take(source.upload(std::as_bytes(std::span(input))));
            std::array<Binding,2> bindings{{{source},{destination}}};std::array<std::uint32_t,3> parameters{257,7,13};
            Session session(device);const auto prepared=take(session.prepare(pipeline,bindings,{5,1,1}));
            auto commands=device.commands();take(session.dispatch(commands,prepared,std::as_bytes(std::span(parameters))));
            take(commands.finish());auto submission=take(device.submit(std::move(commands)));
            auto ticket=completions.watch(submission,[&,destination](Result<void> result) {
                const auto posted=sender.post([&,destination,result=std::move(result)] {
                    if(!result||!destination.read(std::as_writable_bytes(std::span(output))))failed=true;
                    else{gpu_completed=true;for(std::size_t i=0;i<input.size();++i)if(output[i]!=input[i]*7+13)failed=true;}
                    host.request_stop();
                });
                if(!posted){failed=true;host.request_stop();}
            });
            if(!ticket){failed=true;host.request_stop();}
        }catch(...){failed=true;host.request_stop();}
    });});
    for(unsigned i=0;i<4;++i) {
        if(!cpu.post([&]{++cpu_completed;})) failed=true;
    }
    if(!timeout||!submitted||access!=ii::runtime::SubmitError::none) {
        host.request_stop();
        (void)gpu->with([](DeviceThread& thread){(void)thread.close();});
        (void)completions.close();
        (void)cpu.join();
        return 3;
    }
    host.run();
    Result<void> worker_closed;
    const auto closed_access=gpu->with([&](DeviceThread& thread){worker_closed=thread.close();});
    auto finished=completions.close();
    auto joined=cpu.join();
    if(unavailable)return 77;
    if(!worker_closed||closed_access!=ii::runtime::SubmitError::none||!finished||!joined||failed||!gpu_completed||cpu_completed!=4) return 4;
    std::cout<<api_name<<" gpu="<<adapter_name
             <<" cpu_workers="<<cpu.stats().workers<<" values="<<output.size()<<'\n';
    return 0;
}
}
int main(int argc,char** argv) {
    try {
        if(argc>1 && std::string_view(argv[1])=="--hardware") {
            const auto backend=argc>2 && std::string_view(argv[2])=="vulkan"
                ? ii::gpu::Backend::vulkan : ii::gpu::Backend::automatic;
            return hardware(backend);
        }
        return contract();
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
