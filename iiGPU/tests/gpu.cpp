#include "../example/Acceleration/Kernel.h"
#include <algorithm>
#include <array>
#include <future>
#include <iostream>
#include <limits>
#include <numeric>

using namespace ii::gpu;
using example::take;
namespace {
void check(bool v,const char* message) { if(!v) throw std::runtime_error(message); }
template<class T> void rejects(Result<T> result,ErrorCode code) {
    check(!result,"expected rejection"); check(result.error().code==code,result.error().message.c_str());
}
Device make(Backend api,Options options={}) {
    options.backend=api;
    return take(Device::create(options));
}
void contracts(Backend api) {
    check(!Device{},"empty device");
    rejects(Device{}.buffer({16}),ErrorCode::wrong_state);
    rejects(Buffer{}.upload({}),ErrorCode::wrong_state);
    rejects(Submission{}.poll(),ErrorCode::wrong_state);
    rejects(CommandList{}.finish(),ErrorCode::wrong_state);
    auto d=make(api);
    check(d.adapter().hardware,"hardware execution required");
    check(d.limits().max_buffer_bytes>=1024,"reported buffer limit");
    rejects(d.buffer({0}),ErrorCode::invalid_argument);
    rejects(d.buffer({std::numeric_limits<std::uint64_t>::max()}),ErrorCode::limit_exceeded);
    auto b=take(d.buffer({16}));
    std::array<std::byte,17> data{};
    rejects(b.upload(data),ErrorCode::invalid_argument);
    rejects(b.read(data),ErrorCode::invalid_argument);
    auto p=example::transform(); p.constants_bytes=3;
    rejects(d.compute_pipeline(p),ErrorCode::invalid_argument);
    p=example::transform(); p.group_size.x=0;
    rejects(d.compute_pipeline(p),ErrorCode::invalid_argument);
    auto other=make(api);
    auto commands=d.commands();
    rejects(commands.copy(b,take(other.buffer({16})),16),ErrorCode::wrong_device);
    take(commands.fill(b,0)); take(commands.finish());
    rejects(commands.fill(b,0),ErrorCode::wrong_state);
    std::stop_source stop; stop.request_stop();
    rejects(d.submit(std::move(commands),stop.get_token()),ErrorCode::cancelled);
    auto done=take(d.submit(std::move(commands))); take(done.wait());
    rejects(d.submit(std::move(commands)),ErrorCode::wrong_state);
}
void transfers(Backend api) {
    auto d=make(api);
    std::array<std::uint32_t,8> input{0,1,2,0xffffffff,4,5,6,7},output{};
    auto a=take(d.buffer({sizeof(input)}));
    auto device=take(d.buffer({sizeof(input),Memory::device}));
    auto b=take(d.buffer({sizeof(input)}));
    take(a.upload(std::as_bytes(std::span(input))));
    rejects(device.read(std::as_writable_bytes(std::span(output))),ErrorCode::unsupported);
    auto c=d.commands();
    rejects(c.copy(a,b,0),ErrorCode::invalid_argument);
    rejects(c.copy(a,b,4,1),ErrorCode::invalid_argument);
    rejects(c.copy(a,a,16,0,4),ErrorCode::invalid_argument);
    take(c.copy(a,device,sizeof(input)));
    take(c.barrier());
    take(c.copy(device,b,sizeof(input)));
    take(c.finish());
    auto s=take(d.submit(std::move(c)));
    rejects(a.upload(std::as_bytes(std::span(input))),ErrorCode::busy);
    take(s.wait()); take(b.read(std::as_writable_bytes(std::span(output))));
    check(input==output,"device-local roundtrip");
    auto fill=d.commands(); take(fill.fill(device,0xaabbccddu,4,8));
    take(fill.barrier()); take(fill.copy(device,b,sizeof(input))); take(fill.finish());
    take(take(d.submit(std::move(fill))).wait());
    take(b.read(std::as_writable_bytes(std::span(output))));
    check(output[0]==input[0]&&output[1]==0xaabbccddu&&output[2]==0xaabbccddu&&output[3]==input[3],"fill pattern and bounds");
}
void transform(const Device& d,const ComputePipeline& p) {
    std::vector<std::uint32_t> input(257),output(input.size());
    std::iota(input.begin(),input.end(),0xffff0000u);
    const std::array<std::uint32_t,3> params{static_cast<std::uint32_t>(input.size()),7,13};
    auto a=take(d.buffer({input.size()*4})), b=take(d.buffer({input.size()*4}));
    take(a.upload(std::as_bytes(std::span(input))));
    const std::array<Binding,2> bindings{{{a},{b}}};
    auto c=d.commands();
    take(c.dispatch(p,bindings,{5,1,1},std::as_bytes(std::span(params))));
    take(c.finish());
    auto submission=take(d.submit(std::move(c)));
    take(submission.wait());
    take(b.read(std::as_writable_bytes(std::span(output))));
    for(std::size_t i=0;i<input.size();++i) check(output[i]==input[i]*7u+13u,"native shader output mismatch");
}
void dispatch(Backend api) {
    auto d=make(api); auto p=take(d.compute_pipeline(example::transform()));
    transform(d,p);
    auto a=take(d.buffer({1024})), b=take(d.buffer({1024}));
    std::array<Binding,2> bindings{{{a},{b}}}; std::array<std::uint32_t,3> constants{256,3,7};
    auto c=d.commands();
    rejects(c.dispatch(p,bindings,{0,1,1},std::as_bytes(std::span(constants))),ErrorCode::invalid_argument);
    rejects(c.dispatch(p,bindings,{4,1,1}),ErrorCode::invalid_argument);
    take(c.dispatch(p,bindings,{4,1,1},std::as_bytes(std::span(constants))));
    take(c.copy(b,a,1024)); take(c.finish());
    rejects(d.submit(std::move(c)),ErrorCode::invalid_argument);
    auto ok=d.commands();
    take(ok.dispatch(p,bindings,{4,1,1},std::as_bytes(std::span(constants))));
    take(ok.barrier()); take(ok.copy(b,a,1024)); take(ok.finish());
    take(take(d.submit(std::move(ok))).wait());
    auto desc=example::transform(); desc.group_size={32,1,1};
    if(api==Backend::vulkan) rejects(d.compute_pipeline(desc),ErrorCode::invalid_argument);
}
void lifecycle(Backend api) {
    auto d=make(api,{.memory_budget=16384,.max_in_flight=1,.max_commands=2});
    auto a=take(d.buffer({4096}));
    rejects(d.buffer({16384}),ErrorCode::limit_exceeded);
    auto c=d.commands(); take(c.fill(a,0)); take(c.barrier());
    rejects(c.barrier(),ErrorCode::limit_exceeded); take(c.finish());
    auto s=take(d.submit(std::move(c)));
    auto second=d.commands(); take(second.fill(a,1)); take(second.finish());
    rejects(d.submit(std::move(second)),ErrorCode::busy);
    a={}; check(d.stats().allocated_bytes>0,"submission retains resources");
    take(s.wait()); s={};
    take(take(d.submit(std::move(second))).wait());
    check(d.stats().in_flight==0,"completion frees in-flight slots");
    auto b=take(d.buffer({512}));
    { auto drop=d.commands(); take(drop.fill(b,0x12345678)); take(drop.finish());
      auto submitted=take(d.submit(std::move(drop))); }
    std::array<std::uint32_t,128> values{}; take(b.read(std::as_writable_bytes(std::span(values))));
    check(values.front()==0x12345678&&values.back()==0x12345678,"last submission handle safely waits");
    check(d.stats().completed==3,"completed accounting");
}
void concurrency(Backend api) {
    auto d=make(api); auto p=take(d.compute_pipeline(example::transform()));
    std::vector<std::future<void>> jobs;
    for(int i=0;i<4;++i) jobs.push_back(std::async(std::launch::async,[d,p] {
        for(int j=0;j<4;++j) transform(d,p);
    }));
    for(auto& j:jobs) j.get();
    check(d.stats().completed==16&&d.stats().in_flight==0,"concurrent queue accounting");
}
void interop(Backend api) {
    auto d=make(api); auto b=take(d.buffer({16})); std::array<Buffer,1> resources{b};
    take(d.native_access(resources,[&](const NativeContext& ctx)->Result<void> {
        check(ctx.backend==api&&ctx.device&&ctx.queue,"native context");
        check(ctx.buffers.size()==1&&ctx.buffers[0],"native buffer lifetime"); return {};
    }));
    auto c=d.commands(); take(c.fill(b,0)); take(c.finish());
    auto s=take(d.submit(std::move(c)));
    rejects(d.native_access(resources,[](const NativeContext&)->Result<void>{return {};}),ErrorCode::busy);
    take(s.wait());
    rejects(d.native_access(resources,[](const NativeContext&)->Result<void>{
        return std::unexpected(Error{ErrorCode::unsupported,"caller-defined extension"});
    }),ErrorCode::unsupported);
}
}
int main(int argc,char** argv) {
    try {
        if(argc!=3) return 2;
        const Backend api=std::string(argv[1])=="metal"?Backend::metal:Backend::vulkan;
        auto probe=Device::create({.backend=api});
        if(!probe) {
            if(probe.error().code==ErrorCode::unsupported||probe.error().code==ErrorCode::unavailable) {
                std::cout<<"SKIP "<<probe.error().message<<'\n'; return 77;
            }
            throw std::runtime_error(probe.error().message);
        }
        const std::string name=argv[2];
        if(name=="contracts") contracts(api); else if(name=="transfers") transfers(api);
        else if(name=="dispatch") dispatch(api); else if(name=="lifecycle") lifecycle(api);
        else if(name=="concurrency") concurrency(api); else if(name=="interop") interop(api); else return 2;
        std::cout<<"EXECUTED "<<backend_name(api)<<" device="<<probe->adapter().name<<" hardware=true case="<<name<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
