#include <iiGPU/Session.h>
#include "../src/Backends/Backend.h"
#include <array>
#include <atomic>
#include <barrier>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ii::gpu;
using namespace ii::gpu::detail;
namespace {
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class T>T take(Result<T> value){if(!value)throw std::runtime_error(value.error().message);return std::move(*value);}
void take(Result<void> value){if(!value)throw std::runtime_error(value.error().message);}
template<class T>void rejects(Result<T> result,ErrorCode code,const char* message){check(!result&&result.error().code==code,message);}
struct Counters {std::atomic<unsigned> created=0,destroyed=0,submitted=0;std::atomic<bool> fail_next=false;std::vector<std::uint32_t> constants;};
struct FakeBuffer final:NativeBuffer {
    std::shared_ptr<Counters> counts;std::vector<std::byte> bytes;
    FakeBuffer(std::shared_ptr<Counters> c,std::size_t size):counts(std::move(c)),bytes(size){}
    ~FakeBuffer()override{++counts->destroyed;}
    void upload(std::span<const std::byte> data,std::uint64_t offset)override{std::copy(data.begin(),data.end(),bytes.begin()+offset);}
    void read(std::span<std::byte> data,std::uint64_t offset)override{std::copy_n(bytes.begin()+offset,data.size(),data.begin());}
    std::uint64_t handle()const override{return 1;}
    std::uint64_t allocation_bytes()const override{return bytes.size();}
};
struct FakePipeline final:NativePipeline{};
struct FakeCompletion final:NativeSubmission {bool wait(std::chrono::nanoseconds)override{return true;}};
struct FakeDevice final:NativeDevice {
    std::shared_ptr<Counters> counts;
    explicit FakeDevice(std::shared_ptr<Counters> c):counts(std::move(c)){
        adapter.backend=Backend::metal;adapter.hardware=true;
        limits.max_buffer_bytes=4096;limits.max_bindings=8;limits.max_constants_bytes=64;
        limits.max_groups={64,64,64};limits.max_group_size={64,64,64};limits.max_threads_per_group=64;limits.binding_offset_alignment=4;
    }
    std::unique_ptr<NativeBuffer> buffer(BufferDesc d)override{return std::make_unique<FakeBuffer>(counts,d.bytes);}
    std::unique_ptr<NativePipeline> pipeline(const ComputeDesc&)override{++counts->created;return std::make_unique<FakePipeline>();}
    std::shared_ptr<NativeSubmission> submit(const std::vector<Command>& commands)override{
        if(counts->fail_next.exchange(false))throw Failure(ErrorCode::backend_failure,"injected submission failure");
        for(const auto& command:commands)if(const auto* dispatch=std::get_if<Dispatch>(&command)){
            check(dispatch->bindings.size()==2&&dispatch->groups==Extent{2,1,1}&&dispatch->constants.size()==4,"prepared command changed its immutable inputs");
            std::uint32_t value=0;std::memcpy(&value,dispatch->constants.data(),4);counts->constants.push_back(value);
        }
        ++counts->submitted;return std::make_shared<FakeCompletion>();
    }
    NativeContext context()const override{return {};}
};
struct Fixture final {
    std::shared_ptr<Counters> counts=std::make_shared<Counters>();
    std::shared_ptr<DeviceState> raw;
    Device device;
    ComputePipeline pipeline;
    Buffer source,target;
    std::array<Binding,2> bindings;
    Fixture(){
        raw=std::make_shared<DeviceState>();raw->native=std::make_unique<FakeDevice>(counts);
        device=DeviceAccess::adopt(raw);
        ComputeDesc desc;desc.metal_source="test shader";desc.bindings={Access::read,Access::write};desc.constants_bytes=4;desc.group_size={1,1,1};
        pipeline=take(device.compute_pipeline(desc));source=take(device.buffer({16}));target=take(device.buffer({16}));
        bindings={Binding{source},Binding{target}};
    }
};
void reuse_and_hazards(){
    Fixture f;Session session(f.device,{.dispatch_capacity=2});auto token=take(session.prepare(f.pipeline,f.bindings,{2,1,1}));
    for(std::uint32_t value:{7u,9u}){
        auto commands=f.device.commands();take(session.dispatch(commands,token,std::as_bytes(std::span(&value,1))));take(commands.finish());
        auto submission=take(f.device.submit(std::move(commands)));take(submission.wait());
    }
    const auto stats=session.stats();
    check(stats.prepared==1&&stats.dispatches==2&&stats.active==1&&f.counts->created==1&&f.counts->constants==std::vector<std::uint32_t>({7,9}),
          "repeated dispatches did not reuse one prepared session configuration");
    auto invalid=f.device.commands();std::uint32_t value=5;
    take(session.dispatch(invalid,token,std::as_bytes(std::span(&value,1))));take(session.dispatch(invalid,token,std::as_bytes(std::span(&value,1))));take(invalid.finish());
    rejects(f.device.submit(std::move(invalid)),ErrorCode::invalid_argument,"prepared commands bypassed hazard validation");
    auto safe=f.device.commands();take(session.dispatch(safe,token,std::as_bytes(std::span(&value,1))));take(safe.barrier());
    take(session.dispatch(safe,token,std::as_bytes(std::span(&value,1))));take(safe.finish());take(take(f.device.submit(std::move(safe))).wait());
}
void lifetime_and_tokens(){
    Fixture f;Session session(f.device,{.dispatch_capacity=1});Session other(f.device);
    auto first=take(session.prepare(f.pipeline,f.bindings,{2,1,1}));
    rejects(session.prepare(f.pipeline,f.bindings,{2,1,1}),ErrorCode::limit_exceeded,"session capacity is not bounded");
    auto foreign=f.device.commands();std::uint32_t value=3;
    rejects(other.dispatch(foreign,first,std::as_bytes(std::span(&value,1))),ErrorCode::invalid_argument,"foreign token was accepted");
    rejects(session.dispatch(foreign,first,{}),ErrorCode::invalid_argument,"incorrect constants were accepted");
    auto recorded=f.device.commands();take(session.dispatch(recorded,first,std::as_bytes(std::span(&value,1))));
    take(session.release(first));auto second=take(session.prepare(f.pipeline,f.bindings,{2,1,1}));
    rejects(session.dispatch(foreign,first,std::as_bytes(std::span(&value,1))),ErrorCode::wrong_state,"stale token revived after slot reuse");
    f.source={};f.target={};f.bindings={};f.pipeline={};
    session.close();session.close();
    rejects(session.dispatch(foreign,second,std::as_bytes(std::span(&value,1))),ErrorCode::wrong_state,"closed session accepted new work");
    check(f.counts->destroyed==0,"closing a session destroyed resources owned by a recorded command");
    take(recorded.finish());take(take(f.device.submit(std::move(recorded))).wait());
    check(f.counts->submitted==1&&f.counts->destroyed==2&&session.stats().active==0&&session.stats().closed,
          "session close invalidated already recorded work or retained completed resources");
}
void invalid_inputs(){
    bool bad=false;try{Session invalid(Device{});}catch(const std::invalid_argument&){bad=true;}
    Fixture f;bool capacity=false;try{Session invalid(f.device,{.dispatch_capacity=0});}catch(const std::invalid_argument&){capacity=true;}
    Session session(f.device);auto wrong=f.bindings;wrong[0].offset=2;
    rejects(session.prepare(f.pipeline,wrong,{2,1,1}),ErrorCode::invalid_argument,"invalid binding was cached");
    rejects(session.prepare(f.pipeline,f.bindings,{0,1,1}),ErrorCode::invalid_argument,"invalid groups were cached");
    check(bad&&capacity&&session.stats().active==0,"invalid session options or preparation retained resources");
}
void release_returns_unreferenced_resources(){
    Fixture f;Session session(f.device);const auto token=take(session.prepare(f.pipeline,f.bindings,{2,1,1}));
    f.source={};f.target={};f.bindings={};f.pipeline={};
    check(f.counts->destroyed==0,"session did not retain its prepared resources");
    take(session.release(token));
    check(f.counts->destroyed==2&&session.stats().active==0&&session.stats().released==1,"released session resources were retained");
    rejects(session.release(token),ErrorCode::wrong_state,"released token could be released twice");
}
void retry_and_wrong_device(){
    Fixture f,other;Session session(f.device);auto token=take(session.prepare(f.pipeline,f.bindings,{2,1,1}));
    std::uint32_t value=11;auto wrong=other.device.commands();
    rejects(session.dispatch(wrong,token,std::as_bytes(std::span(&value,1))),ErrorCode::wrong_device,"prepared dispatch crossed device ownership");
    auto commands=f.device.commands();take(session.dispatch(commands,token,std::as_bytes(std::span(&value,1))));take(commands.finish());
    f.counts->fail_next=true;rejects(f.device.submit(std::move(commands)),ErrorCode::backend_failure,"native error was hidden");
    check(f.raw->jobs.empty()&&f.device.stats().in_flight==0,"failed admission left an in-flight tracker");
    take(take(f.device.submit(std::move(commands))).wait());
    check(f.raw->jobs.empty()&&f.device.stats().completed==1,"failed submission could not be retried or retired");
}
void indexed_inflight_and_collection(){
    Fixture f;Session session(f.device);auto token=take(session.prepare(f.pipeline,f.bindings,{2,1,1}));
    std::uint32_t value=12;std::vector<Submission> jobs;
    for(unsigned i=0;i<3;++i){auto commands=f.device.commands();take(session.dispatch(commands,token,std::as_bytes(std::span(&value,1))));
        take(commands.finish());jobs.push_back(take(f.device.submit(std::move(commands))));}
    check(f.raw->jobs.size()==3&&f.device.stats().in_flight==3,"each accepted submission needs one tracked identity");
    take(jobs[1].wait());check(f.raw->jobs.size()==2,"out-of-order completion retired the wrong tracker");
    take(f.device.collect());check(f.raw->jobs.empty()&&f.device.stats().completed==3,"collection lost or retained a completed tracker");
    auto cancelled=f.device.commands();take(session.dispatch(cancelled,token,std::as_bytes(std::span(&value,1))));take(cancelled.finish());
    std::stop_source stop;stop.request_stop();
    rejects(f.device.submit(std::move(cancelled),stop.get_token()),ErrorCode::cancelled,"pre-admission cancellation was hidden");
    check(f.raw->jobs.empty(),"cancelled admission left an in-flight tracker");take(take(f.device.submit(std::move(cancelled))).wait());
}
void concurrent_recording(){
    Fixture f;Session session(f.device);const auto token=take(session.prepare(f.pipeline,f.bindings,{2,1,1}));
    std::array<std::thread,8> threads;std::atomic<unsigned> succeeded=0;
    for(auto& thread:threads)thread=std::thread([&]{std::uint32_t value=1;auto commands=f.device.commands();
        if(session.dispatch(commands,token,std::as_bytes(std::span(&value,1)))&&commands.finish())++succeeded;});
    for(auto& thread:threads)thread.join();
    check(succeeded==threads.size()&&session.stats().dispatches==threads.size(),"concurrent session use lost dispatches");
}
void barrier_and_in_place_copy(){
    Fixture f;
    auto in_place=f.device.commands();take(in_place.copy(f.source,f.source,4,0,4));take(in_place.finish());
    take(take(f.device.submit(std::move(in_place))).wait());
    auto hazard=f.device.commands();take(hazard.copy(f.source,f.target,4));take(hazard.copy(f.target,f.source,4));take(hazard.finish());
    rejects(f.device.submit(std::move(hazard)),ErrorCode::invalid_argument,"cross-command transfer hazard was missed");
    auto safe=f.device.commands();take(safe.copy(f.source,f.target,4));take(safe.barrier());take(safe.copy(f.target,f.source,4));take(safe.finish());
    take(take(f.device.submit(std::move(safe))).wait());
    auto prior_write=f.device.commands();take(prior_write.fill(f.source,1,0,4));take(prior_write.copy(f.source,f.source,4,0,4));take(prior_write.finish());
    rejects(f.device.submit(std::move(prior_write)),ErrorCode::invalid_argument,"in-place copy hid an earlier write hazard");
}
void concurrent_completion_and_collect(){
    Fixture f;Session session(f.device);const auto token=take(session.prepare(f.pipeline,f.bindings,{2,1,1}));
    constexpr std::size_t count=8;std::array<Submission,count> jobs;std::uint32_t value=1;
    for(auto& job:jobs){auto commands=f.device.commands();take(session.dispatch(commands,token,std::as_bytes(std::span(&value,1))));
        take(commands.finish());job=take(f.device.submit(std::move(commands)));}
    std::barrier start(static_cast<std::ptrdiff_t>(count+1));std::array<std::thread,count> threads;std::atomic<unsigned> complete=0;
    for(std::size_t i=0;i<count;++i)threads[i]=std::thread([&,i]{start.arrive_and_wait();if(jobs[i].wait())++complete;});
    start.arrive_and_wait();const auto collected=f.device.collect();
    for(auto& thread:threads)thread.join();
    check(collected&&complete==count&&f.raw->jobs.empty()&&f.device.stats().in_flight==0&&f.device.stats().completed==count,
          "concurrent collection and completion lost or duplicated a tracked submission");
}
}
int main(){try{reuse_and_hazards();lifetime_and_tokens();invalid_inputs();release_returns_unreferenced_resources();retry_and_wrong_device();indexed_inflight_and_collection();concurrent_recording();barrier_and_in_place_copy();concurrent_completion_and_collect();}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
