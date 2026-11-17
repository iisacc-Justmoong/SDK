#include "../example/CooperativeChunks/Work.h"
#include "../example/Acceleration/Kernel.h"
#include "../src/Backends/Backend.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
using namespace ii::gpu;
using namespace ii::gpu::detail;
using namespace ii::runtime;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class T> T gpu_take(Result<T> value) {if(!value)throw std::runtime_error(value.error().message);return std::move(*value);}
struct Gate final {
    std::mutex mutex;std::condition_variable cv;bool entered=false,open=false;
    void wait() {std::unique_lock lock(mutex);entered=true;cv.notify_all();cv.wait(lock,[&]{return open;});}
    void await() {std::unique_lock lock(mutex);check(cv.wait_for(lock,3s,[&]{return entered;}),"lane did not enter");}
    void release(){ {std::lock_guard lock(mutex);open=true;}cv.notify_all(); }
};
struct FakeBuffer final:NativeBuffer {
    explicit FakeBuffer(std::size_t size):data(size){}
    std::vector<std::byte> data;
    void upload(std::span<const std::byte> in,std::uint64_t offset) override {std::copy(in.begin(),in.end(),data.begin()+offset);}
    void read(std::span<std::byte> out,std::uint64_t offset) override {std::copy_n(data.begin()+offset,out.size(),out.begin());}
    std::uint64_t handle()const override{return 1;}
    std::uint64_t allocation_bytes()const override{return data.size();}
};
struct FakePipeline final:NativePipeline {};
struct FakeSubmission final:NativeSubmission {
    explicit FakeSubmission(std::shared_ptr<Gate> value):gate(std::move(value)){}
    std::shared_ptr<Gate> gate;
    bool wait(std::chrono::nanoseconds)override{gate->wait();return true;}
};
struct FakeDevice final:NativeDevice {
    explicit FakeDevice(std::shared_ptr<Gate> value):gate(std::move(value)) {
        adapter={Backend::metal,0,"mock",DeviceKind::integrated,true};
        limits.max_buffer_bytes=4096;limits.max_bindings=8;limits.max_constants_bytes=64;
        limits.max_group_size={64,1,1};limits.max_groups={2,1,1};limits.max_threads_per_group=64;limits.binding_offset_alignment=4;
    }
    std::shared_ptr<Gate> gate;
    std::unique_ptr<NativeBuffer> buffer(BufferDesc d)override{return std::make_unique<FakeBuffer>(d.bytes);}
    std::unique_ptr<NativePipeline> pipeline(const ComputeDesc&)override{return std::make_unique<FakePipeline>();}
    std::shared_ptr<NativeSubmission> submit(const std::vector<Command>& commands)override {
        for(const auto& command:commands)if(const auto* d=std::get_if<Dispatch>(&command)) {
            std::array<std::uint32_t,3> constants{};std::memcpy(constants.data(),d->constants.data(),sizeof(constants));
            check(constants[0]<=128 && d->groups.x==(constants[0]+63)/64,"GPU group geometry invalid");
            auto* in=static_cast<FakeBuffer*>(d->bindings[0].buffer->native.get());
            auto* out=static_cast<FakeBuffer*>(d->bindings[1].buffer->native.get());
            for(std::uint32_t i=0;i<constants[0];++i) {
                std::uint32_t value;std::memcpy(&value,in->data.data()+i*4,4);
                value=value*constants[1]+constants[2];std::memcpy(out->data.data()+i*4,&value,4);
            }
        }
        return std::make_shared<FakeSubmission>(gate);
    }
    NativeContext context()const override{return {};}
};
void run() {
    auto cpu_gate=std::make_shared<Gate>(),gpu_gate=std::make_shared<Gate>();
    auto state=std::make_shared<DeviceState>();state->options.memory_budget=1<<20;state->native=std::make_unique<FakeDevice>(gpu_gate);
    auto device=DeviceAccess::adopt(state);
    auto pipeline=gpu_take(device.compute_pipeline(example::transform()));
    std::vector<std::uint32_t> input(4096),output(input.size());std::iota(input.begin(),input.end(),0u);
    ChunkQueue queue({.cpu_workers=1,.capacity=4,.devices={{64,2}}});
    auto work=example::transform_work(device,pipeline,input,output,128);
    auto cpu=std::move(work.cpu);
    work.cpu=[cpu=std::move(cpu),cpu_gate](ChunkRange range,std::stop_token stop)->ii::core::Result<void>{
        cpu_gate->wait();return cpu(range,stop);
    };
    auto accepted=queue.submit(std::move(work));check(bool(accepted),"application work was rejected");
    auto task=std::move(*accepted);
    try {
        cpu_gate->await();gpu_gate->await();
        const auto running=task.snapshot();
        check(running.cpu_active==1 && running.device_in_flight==1 && running.device_submitted==1,
              "CPU and submitted GPU were not simultaneously observable");
    } catch(...) {cpu_gate->release();gpu_gate->release();throw;}
    cpu_gate->release();gpu_gate->release();
    auto result=task.snapshot();const auto deadline=std::chrono::steady_clock::now()+5s;
    while(result.phase!=ChunkPhase::completed && result.phase!=ChunkPhase::failed && std::chrono::steady_clock::now()<deadline)
        result=task.wait_for_change(result.revision,100ms);
    check(result.phase==ChunkPhase::completed && result.completed_items==input.size(),"mixed task did not complete");
    for(std::size_t i=0;i<input.size();++i)check(output[i]==input[i]*7+13,"CPU/GPU result mismatch");
    check(bool(queue.close()),"queue shutdown failed");
}
}
int main(){try{run();std::cout<<"mock iiGPU and Core chunk integration passed\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
