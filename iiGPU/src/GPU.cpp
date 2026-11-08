#include "Backend.h"
#include <limits>
#include <unordered_map>

namespace ii::gpu {
using namespace detail;
namespace {
bool has(Usage all,Usage flag) { return (static_cast<unsigned>(all)&static_cast<unsigned>(flag))!=0; }
void range(std::uint64_t size,std::uint64_t offset,std::uint64_t count) {
    require(offset<=size&&count<=size-offset,ErrorCode::invalid_argument,"buffer range exceeds allocation");
}
void recording(const std::unique_ptr<CommandsState>& s) {
    require(s&&s->owner&&s->phase==CommandsState::Phase::recording,ErrorCode::wrong_state,"command list is not recording");
    require(s->commands.size()<s->owner->options.max_commands,ErrorCode::limit_exceeded,"command-list capacity exceeded");
}
void owned(const std::shared_ptr<DeviceState>& owner,const std::shared_ptr<BufferState>& b) {
    require(bool(b),ErrorCode::invalid_argument,"empty buffer");
    require(b->owner==owner,ErrorCode::wrong_device,"resource belongs to another device");
}
void aligned(std::uint64_t n) { require(n%4==0,ErrorCode::invalid_argument,"GPU transfer and storage ranges must be aligned to four bytes"); }
struct Validation final {
    std::vector<std::shared_ptr<BufferState>> resources;
    std::vector<std::shared_ptr<PipelineState>> pipelines;
};
Validation validate(const CommandsState& s) {
    struct AccessState final {
        std::shared_ptr<BufferState> resource;Access last;std::uint64_t epoch;
        AccessState(const std::shared_ptr<BufferState>& value,Access mode,std::uint64_t generation)
            :resource(value),last(mode),epoch(generation){}
    };
    std::unordered_map<BufferState*,AccessState> resources;
    std::uint64_t epoch=1;
    Validation result;
    auto access=[&](const std::shared_ptr<BufferState>& b,Access a,bool same_copy=false) {
        auto [it,inserted]=resources.try_emplace(b.get(),b,a,epoch);
        if(!inserted){
            require(it->second.epoch!=epoch||same_copy||(it->second.last==Access::read&&a==Access::read),
                ErrorCode::invalid_argument,"buffer hazard requires an explicit memory barrier");
            it->second.last=a;it->second.epoch=epoch;
        }
    };
    for(const auto& command:s.commands) {
        if(const auto* copy=std::get_if<Copy>(&command)) {
            access(copy->source,Access::read);
            // A validated nonoverlapping in-place copy admits its own write;
            // the preceding read still checks hazards from earlier commands.
            access(copy->destination,Access::write,copy->source==copy->destination);
        } else if(const auto* fill=std::get_if<Fill>(&command)) access(fill->buffer,Access::write);
        else if(const auto* dispatch=std::get_if<Dispatch>(&command)) {
            result.pipelines.push_back(dispatch->pipeline);
            for(std::size_t i=0;i<dispatch->bindings.size();++i)
                access(dispatch->bindings[i].buffer,dispatch->pipeline->desc.bindings[i]);
        } else if(epoch==(std::numeric_limits<std::uint64_t>::max)()) {
            for(auto& [key,value]:resources){(void)key;value.epoch=0;}
            epoch=1;
        } else ++epoch;
    }
    result.resources.reserve(resources.size());
    for(auto& [key,value]:resources) { (void)key; result.resources.push_back(std::move(value.resource)); }
    return result;
}
void validate_pipeline(const ComputeDesc& d,const Limits& l,Backend api) {
    require(!d.bindings.empty()&&d.bindings.size()<=l.max_bindings,ErrorCode::limit_exceeded,"storage binding count exceeds device limits");
    require(d.constants_bytes%4==0,ErrorCode::invalid_argument,"constants size must be a multiple of four");
    require(d.constants_bytes<=l.max_constants_bytes,ErrorCode::limit_exceeded,"constants exceed device limits");
    for(auto access:d.bindings) require(access==Access::read||access==Access::write||access==Access::read_write,
        ErrorCode::invalid_argument,"invalid storage buffer access mode");
    require(d.group_size.x&&d.group_size.y&&d.group_size.z,ErrorCode::invalid_argument,"zero workgroup dimension");
    require(d.group_size.x<=l.max_group_size.x&&d.group_size.y<=l.max_group_size.y&&d.group_size.z<=l.max_group_size.z,
        ErrorCode::limit_exceeded,"workgroup dimensions exceed device limits");
    const auto threads=std::uint64_t(d.group_size.x)*d.group_size.y;
    require(threads<=l.max_threads_per_group&&d.group_size.z<=l.max_threads_per_group/threads,
        ErrorCode::limit_exceeded,"workgroup thread count exceeds device limits");
    if(api==Backend::vulkan) {
        require(d.spirv.size()>=5&&d.spirv[0]==0x07230203u,ErrorCode::invalid_argument,"SPIR-V header is invalid");
        bool local_size=false;
        for(std::size_t at=5;at<d.spirv.size();) {
            const auto count=d.spirv[at]>>16,op=d.spirv[at]&0xffff;
            require(count&&count<=d.spirv.size()-at,ErrorCode::invalid_argument,"SPIR-V instruction is truncated");
            if(op==16&&count==6&&d.spirv[at+2]==17) {
                require(Extent{d.spirv[at+3],d.spirv[at+4],d.spirv[at+5]}==d.group_size,
                    ErrorCode::invalid_argument,"declared workgroup size differs from SPIR-V LocalSize");
                local_size=true;
            }
            at+=count;
        }
        require(local_size,ErrorCode::unsupported,"SPIR-V must declare a fixed LocalSize execution mode");
        require(!d.vulkan_entry.empty(),ErrorCode::invalid_argument,"Vulkan entry point is empty");
    } else {
        require(!d.metal_source.empty()||!d.metal_library.empty(),ErrorCode::invalid_argument,"Metal shader code is empty");
        require(!d.metal_entry.empty(),ErrorCode::invalid_argument,"Metal entry point is empty");
    }
}
}
const char* backend_name(Backend api) noexcept {
    switch(api) { case Backend::metal:return "Metal"; case Backend::vulkan:return "Vulkan"; default:return "automatic"; }
}
std::vector<Backend> Device::compiled_backends() {
    std::vector<Backend> result;
#ifdef IIGPU_HAS_METAL
    result.push_back(Backend::metal);
#endif
#ifdef IIGPU_HAS_VULKAN
    result.push_back(Backend::vulkan);
#endif
    return result;
}
Result<std::vector<Adapter>> Device::adapters(Backend api) {
    return protect([&] {
#ifdef IIGPU_HAS_METAL
        if(api==Backend::metal) return metal_adapters();
#endif
#ifdef IIGPU_HAS_VULKAN
        if(api==Backend::vulkan) return vulkan_adapters();
#endif
        if(api==Backend::automatic) {
            std::vector<Adapter> result;
            for(auto backend:compiled_backends()) {
                auto found=adapters(backend);
                if(found) result.insert(result.end(),found->begin(),found->end());
            }
            return result;
        }
        fail(ErrorCode::unsupported,"GPU backend was not compiled");
    });
}
Result<Device> Device::create(Options options) {
    return protect([&] {
        require(options.memory_budget&&options.max_in_flight&&options.max_commands,
            ErrorCode::invalid_argument,"GPU budgets must be positive");
        auto backends=options.backend==Backend::automatic?compiled_backends():std::vector{options.backend};
        require(!backends.empty(),ErrorCode::unsupported,"no GPU backend was compiled");
        std::optional<Error> error;
        for(auto api:backends) {
            (void)api;
            try {
                auto state=std::make_shared<DeviceState>(); state->options=options;
#ifdef IIGPU_HAS_METAL
                if(api==Backend::metal) state->native=metal_device(options);
#endif
#ifdef IIGPU_HAS_VULKAN
                if(api==Backend::vulkan) state->native=vulkan_device(options);
#endif
                require(bool(state->native),ErrorCode::unsupported,"GPU backend was not compiled");
                require(!options.require_hardware||state->native->adapter.hardware,
                    ErrorCode::unavailable,"selected adapter is not a physical GPU");
                return DeviceAccess::adopt(std::move(state));
            } catch(const Failure& e) { error=e.error; }
        }
        throw Failure(error->code,error->message);
    });
}
Adapter Device::adapter() const { return state_?state_->native->adapter:Adapter{}; }
Limits Device::limits() const { return state_?state_->native->limits:Limits{}; }
Stats Device::stats() const {
    if(!state_) return {};
    std::lock_guard lock(state_->mutex);
    auto stats=state_->stats; stats.allocated_bytes=state_->allocated.load(); return stats;
}
Result<std::uint64_t> Device::memory_budget() const {
    return protect([&] {
        require(bool(state_),ErrorCode::wrong_state,"empty GPU device");
        std::lock_guard lock(state_->mutex);return state_->options.memory_budget;
    });
}
Result<void> Device::set_memory_budget(std::uint64_t bytes) const {
    return protect_void([&] {
        require(bool(state_),ErrorCode::wrong_state,"empty GPU device");
        std::lock_guard lock(state_->mutex);
        require(bytes>=state_->allocated.load(),ErrorCode::limit_exceeded,"new budget is below live native allocations");
        state_->options.memory_budget=bytes;
    });
}
BufferState::~BufferState() { native.reset(); if(owner&&charged) owner->allocated.fetch_sub(charged); }
Result<Buffer> Device::buffer(BufferDesc desc) const {
    return protect([&] {
        require(bool(state_),ErrorCode::wrong_state,"empty GPU device");
        require(desc.bytes>0,ErrorCode::invalid_argument,"zero-size GPU buffer");
        require(desc.memory==Memory::shared||desc.memory==Memory::device,ErrorCode::invalid_argument,"invalid GPU memory mode");
        require(desc.bytes<=state_->native->limits.max_buffer_bytes,ErrorCode::limit_exceeded,"buffer exceeds device limit");
        const auto usage=static_cast<unsigned>(desc.usage);
        require(usage&&(usage&~7u)==0,ErrorCode::invalid_argument,"invalid buffer usage flags");
        std::lock_guard lock(state_->mutex);
        const auto remaining=state_->options.memory_budget-state_->allocated.load();
        require(desc.bytes<=remaining,ErrorCode::limit_exceeded,"GPU buffer memory budget exceeded");
        auto buffer=std::make_shared<BufferState>(); buffer->owner=state_; buffer->desc=desc;
        buffer->native=state_->native->buffer(desc);
        const auto bytes=buffer->native->allocation_bytes();
        require(bytes<=remaining,ErrorCode::limit_exceeded,"native allocation alignment exceeds GPU memory budget");
        buffer->charged=bytes; state_->allocated.fetch_add(bytes);
        return Buffer(std::move(buffer));
    });
}
Result<Buffer> Device::import_buffer(const Buffer& source,BufferTransferOptions options,std::stop_token stop) const {
    return protect([&] {
        require(bool(state_)&&bool(source.state_),ErrorCode::wrong_state,"empty source buffer or destination device");
        require(!stop.stop_requested(),ErrorCode::cancelled,"transfer cancelled before admission");
        require(options.chunk_bytes>0&&options.chunk_bytes<=std::numeric_limits<std::size_t>::max(),
            ErrorCode::invalid_argument,"invalid transfer chunk size");
        const auto& desc=source.state_->desc;
        const bool source_device=desc.memory==Memory::device,destination_device=options.memory==Memory::device;
        if(source_device||destination_device){
            require(desc.bytes%4==0&&options.chunk_bytes%4==0,ErrorCode::invalid_argument,"device transfer requires four-byte alignment");
            if(source_device) require(has(desc.usage,Usage::transfer_source),ErrorCode::invalid_argument,"source lacks transfer-source usage");
            if(destination_device) require(has(options.usage,Usage::transfer_destination),ErrorCode::invalid_argument,"destination lacks transfer-destination usage");
        }
        {
            std::lock_guard lock(source.state_->owner->mutex);
            require(!source.state_->pending,ErrorCode::busy,"source has an unobserved GPU submission");
        }
        auto take=[]<class T>(Result<T> value)->T {
            if(!value)throw Failure(value.error().code,value.error().message);
            return std::move(*value);
        };
        auto done=[](Result<void> value){if(!value)throw Failure(value.error().code,value.error().message);};
        auto destination=take(buffer({desc.bytes,options.memory,options.usage}));
        const auto chunk=std::min(desc.bytes,options.chunk_bytes);
        std::vector<std::byte> host(static_cast<std::size_t>(chunk));
        Device source_owner(source.state_->owner);
        Buffer source_stage,destination_stage;
        if(source_device) source_stage=take(source_owner.buffer({chunk,Memory::shared,Usage::transfer_destination}));
        if(destination_device) destination_stage=take(buffer({chunk,Memory::shared,Usage::transfer_source}));
        for(std::uint64_t offset=0;offset<desc.bytes;){
            require(!stop.stop_requested(),ErrorCode::cancelled,"transfer cancelled between chunks");
            const auto bytes=std::min(chunk,desc.bytes-offset);
            auto view=std::span<std::byte>(host.data(),static_cast<std::size_t>(bytes));
            if(source_device){
                auto commands=source_owner.commands();done(commands.copy(source,source_stage,bytes,offset));done(commands.finish());
                auto submitted=take(source_owner.submit(std::move(commands),stop));done(submitted.wait());done(source_stage.read(view));
            } else done(source.read(view,offset));
            require(!stop.stop_requested(),ErrorCode::cancelled,"transfer cancelled before destination write");
            if(destination_device){
                done(destination_stage.upload(view));auto commands=this->commands();
                done(commands.copy(destination_stage,destination,bytes,0,offset));done(commands.finish());
                auto submitted=take(submit(std::move(commands),stop));done(submitted.wait());
            } else done(destination.upload(view,offset));
            offset+=bytes;
        }
        return destination;
    });
}
Result<Buffer> Device::move_buffer(Buffer& source,BufferTransferOptions options,std::stop_token stop) const {
    auto result=import_buffer(source,options,stop);if(result)source={};return result;
}
std::uint64_t Buffer::size() const noexcept { return state_?state_->desc.bytes:0; }
Result<void> Buffer::upload(std::span<const std::byte> bytes,std::uint64_t offset) const {
    return protect_void([&] {
        require(bool(state_),ErrorCode::wrong_state,"empty GPU buffer");
        range(size(),offset,bytes.size());
        require(state_->desc.memory==Memory::shared,ErrorCode::unsupported,"device-only buffer requires a staging transfer");
        std::lock_guard lock(state_->owner->mutex);
        require(!state_->pending,ErrorCode::busy,"GPU still owns this buffer; observe submission completion first");
        state_->native->upload(bytes,offset);
    });
}
Result<void> Buffer::read(std::span<std::byte> bytes,std::uint64_t offset) const {
    return protect_void([&] {
        require(bool(state_),ErrorCode::wrong_state,"empty GPU buffer");
        range(size(),offset,bytes.size());
        require(state_->desc.memory==Memory::shared,ErrorCode::unsupported,"device-only buffer requires a staging transfer");
        std::lock_guard lock(state_->owner->mutex);
        require(!state_->pending,ErrorCode::busy,"GPU still owns this buffer; observe submission completion first");
        state_->native->read(bytes,offset);
    });
}
Result<ComputePipeline> Device::compute_pipeline(const ComputeDesc& desc) const {
    return protect([&] {
        require(bool(state_),ErrorCode::wrong_state,"empty GPU device");
        validate_pipeline(desc,state_->native->limits,state_->native->adapter.backend);
        auto pipeline=std::make_shared<PipelineState>(); pipeline->owner=state_;
        // Retain only the command ABI after compilation, not a second copy of
        // potentially large source/metallib/SPIR-V payloads.
        pipeline->desc.bindings=desc.bindings;
        pipeline->desc.constants_bytes=desc.constants_bytes;
        pipeline->desc.group_size=desc.group_size;
        std::lock_guard lock(state_->mutex);
        pipeline->native=state_->native->pipeline(desc);
        return ComputePipeline(std::move(pipeline));
    });
}
CommandList::CommandList()=default;
CommandList::~CommandList()=default;
CommandList::CommandList(CommandList&&) noexcept=default;
CommandList& CommandList::operator=(CommandList&&) noexcept=default;
CommandList::CommandList(std::unique_ptr<CommandsState> s):state_(std::move(s)) {}
CommandList Device::commands() const {
    auto commands=std::make_unique<CommandsState>(); commands->owner=state_;
    return CommandList(std::move(commands));
}
Result<void> CommandList::copy(const Buffer& a,const Buffer& b,std::uint64_t bytes,std::uint64_t ao,std::uint64_t bo) {
    return protect_void([&] {
        recording(state_); owned(state_->owner,a.state_); owned(state_->owner,b.state_);
        require(bytes>0,ErrorCode::invalid_argument,"zero-size transfer");
        aligned(bytes); aligned(ao); aligned(bo); range(a.size(),ao,bytes); range(b.size(),bo,bytes);
        require(has(a.state_->desc.usage,Usage::transfer_source)&&has(b.state_->desc.usage,Usage::transfer_destination),
            ErrorCode::invalid_argument,"transfer usage was not declared");
        require(a.state_!=b.state_||ao+bytes<=bo||bo+bytes<=ao,ErrorCode::invalid_argument,"overlapping buffer copy");
        state_->commands.push_back(Copy{a.state_,b.state_,bytes,ao,bo});
    });
}
Result<void> CommandList::fill(const Buffer& b,std::uint32_t pattern,std::uint64_t offset,std::uint64_t bytes) {
    return protect_void([&] {
        recording(state_); owned(state_->owner,b.state_);
        require(offset<=b.size(),ErrorCode::invalid_argument,"fill offset exceeds buffer");
        if(!bytes) bytes=b.size()-offset;
        require(bytes>0,ErrorCode::invalid_argument,"zero-size fill");
        aligned(offset); aligned(bytes); range(b.size(),offset,bytes);
        require(has(b.state_->desc.usage,Usage::transfer_destination),ErrorCode::invalid_argument,"fill requires transfer destination usage");
        state_->commands.push_back(Fill{b.state_,pattern,offset,bytes});
    });
}
Result<void> CommandList::dispatch(const ComputePipeline& p,std::span<const Binding> bindings,Extent groups,std::span<const std::byte> constants) {
    return protect_void([&] {
        recording(state_);
        require(bool(p.state_),ErrorCode::invalid_argument,"empty compute pipeline");
        require(p.state_->owner==state_->owner,ErrorCode::wrong_device,"pipeline belongs to another device");
        const auto& limits=state_->owner->native->limits;
        require(groups.x&&groups.y&&groups.z,ErrorCode::invalid_argument,"zero dispatch dimension");
        require(groups.x<=limits.max_groups.x&&groups.y<=limits.max_groups.y&&groups.z<=limits.max_groups.z,
            ErrorCode::limit_exceeded,"dispatch exceeds device limits");
        const auto group=p.state_->desc.group_size;
        require(groups.x<=UINT32_MAX/group.x&&groups.y<=UINT32_MAX/group.y&&groups.z<=UINT32_MAX/group.z,
            ErrorCode::limit_exceeded,"global invocation coordinates exceed uint32 addressing");
        require(bindings.size()==p.state_->desc.bindings.size()&&constants.size()==p.state_->desc.constants_bytes,
            ErrorCode::invalid_argument,"compute bindings or constants differ from pipeline layout");
        Dispatch command{p.state_,{},groups,{constants.begin(),constants.end()}};
        for(const auto& binding:bindings) {
            owned(state_->owner,binding.buffer.state_);
            require(binding.offset<binding.buffer.size(),ErrorCode::invalid_argument,"storage binding offset exceeds buffer");
            const auto bytes=binding.bytes?binding.bytes:binding.buffer.size()-binding.offset;
            range(binding.buffer.size(),binding.offset,bytes); aligned(bytes);
            require(binding.offset%limits.binding_offset_alignment==0,ErrorCode::invalid_argument,"storage binding offset is not aligned");
            require(has(binding.buffer.state_->desc.usage,Usage::storage),ErrorCode::invalid_argument,"storage usage was not declared");
            command.bindings.push_back({binding.buffer.state_,binding.offset,bytes});
        }
        state_->commands.push_back(std::move(command));
    });
}
Result<void> CommandList::barrier() { return protect_void([&] {recording(state_); state_->commands.push_back(Barrier{});}); }
Result<void> CommandList::append_prepared(const Dispatch& prepared,std::span<const std::byte> constants) {
    return protect_void([&] {
        recording(state_);
        require(prepared.pipeline->owner==state_->owner,ErrorCode::wrong_device,"prepared dispatch belongs to another device");
        require(constants.size()==prepared.pipeline->desc.constants_bytes,ErrorCode::invalid_argument,"constants differ from pipeline layout");
        Dispatch command{prepared.pipeline,prepared.bindings,prepared.groups,{constants.begin(),constants.end()}};
        state_->commands.push_back(std::move(command));
    });
}
Result<void> CommandList::finish() {
    return protect_void([&] {
        require(state_&&state_->owner&&state_->phase==CommandsState::Phase::recording,ErrorCode::wrong_state,"command list is not recording");
        require(!state_->commands.empty(),ErrorCode::invalid_argument,"empty command list");
        state_->phase=CommandsState::Phase::executable;
    });
}
Result<Submission> Device::submit(CommandList&& list,std::stop_token stop) const {
    return protect([&] {
        require(state_&&list.state_&&list.state_->phase==CommandsState::Phase::executable,ErrorCode::wrong_state,"command list must be executable and unused");
        require(state_==list.state_->owner,ErrorCode::wrong_device,"command list belongs to another device");
        require(!stop.stop_requested(),ErrorCode::cancelled,"GPU submission cancelled before admission");
        auto validated=validate(*list.state_);
        auto job=std::make_shared<SubmissionState>(); job->owner=state_;
        job->resources=std::move(validated.resources);job->pipelines=std::move(validated.pipelines);
        std::lock_guard lock(state_->mutex);
        require(state_->stats.in_flight<state_->options.max_in_flight,ErrorCode::busy,"GPU submission capacity exceeded; observe completion or collect");
        // Allocate the indexed entry before committing native work. A failed
        // admission removes it, and retirement erases it by identity.
        const auto [tracked,inserted]=state_->jobs.emplace(job.get(),job);
        require(inserted,ErrorCode::backend_failure,"duplicate GPU submission identity");
        try {
            require(!stop.stop_requested(),ErrorCode::cancelled,"GPU submission cancelled before native commit");
            job->native=state_->native->submit(list.state_->commands);
        }catch(...){state_->jobs.erase(tracked);throw;}
        for(const auto& b:job->resources) ++b->pending;
        ++state_->stats.in_flight; ++state_->stats.submissions;
        list.state_->phase=CommandsState::Phase::submitted;
        list.state_->commands.clear();
        return Submission(std::move(job));
    });
}
void SubmissionState::retire() {
    if(retired) return;
    std::lock_guard lock(owner->mutex);
    for(const auto& b:resources) --b->pending;
    --owner->stats.in_flight;
    if(error) ++owner->stats.failed; else ++owner->stats.completed;
    // Keeping a completed public handle must not keep driver command pools,
    // descriptors or a growing weak-entry list alive.
    native.reset();
    owner->jobs.erase(this);
    resources.clear(); pipelines.clear(); retired=true;
}
SubmissionState::~SubmissionState() {
    if(native&&!retired) (void)wait(std::chrono::nanoseconds::max());
}
Result<bool> SubmissionState::wait(std::chrono::nanoseconds timeout) {
    std::shared_ptr<NativeSubmission> submitted;
    {
        std::lock_guard lock(mutex);
        if(retired) {
            if(error) return std::unexpected(*error);
            return true;
        }
        submitted=native;
    }
    // Native waits can block; keep their handle alive without holding the
    // bookkeeping mutex, so a concurrent zero-time poll stays nonblocking.
    auto result=protect([&]{return submitted->wait(timeout);});
    {
        std::lock_guard lock(mutex);
        if(!retired) {
            if(!result) { error=result.error(); retire(); }
            else if(*result) retire();
            else return false;
        }
        if(error) return std::unexpected(*error);
    }
    return true;
}
Result<bool> Submission::poll() const {
    if(!state_) return std::unexpected(Error{ErrorCode::wrong_state,"empty GPU submission"});
    return state_->wait(std::chrono::nanoseconds::zero());
}
Result<void> Submission::wait(std::chrono::nanoseconds timeout) const {
    if(!state_) return std::unexpected(Error{ErrorCode::wrong_state,"empty GPU submission"});
    if(timeout.count()<0) return std::unexpected(Error{ErrorCode::invalid_argument,"negative GPU wait timeout"});
    auto result=state_->wait(timeout);
    if(!result) return std::unexpected(result.error());
    if(!*result) return std::unexpected(Error{ErrorCode::timeout,"GPU wait timed out; work remains submitted"});
    return {};
}
Result<void> Device::collect() const {
    if(!state_) return std::unexpected(Error{ErrorCode::wrong_state,"empty GPU device"});
    std::vector<std::shared_ptr<SubmissionState>> jobs;
    { std::lock_guard lock(state_->mutex);
      // Reserve before promoting weak handles. An allocation failure while
      // holding the device lock must not drop the final pending job handle.
      jobs.reserve(state_->jobs.size());
      for(const auto& [key,weak]:state_->jobs) { (void)key;if(auto job=weak.lock()) jobs.push_back(std::move(job)); } }
    for(const auto& job:jobs) {auto result=job->wait(std::chrono::nanoseconds::zero()); if(!result) return std::unexpected(result.error());}
    return {};
}
Result<void> Device::native_access(std::span<const Buffer> buffers,const std::function<Result<void>(const NativeContext&)>& callback) const {
    if(!callback) return std::unexpected(Error{ErrorCode::invalid_argument,"empty native access callback"});
    auto result=protect([&] {
        require(bool(state_),ErrorCode::wrong_state,"empty GPU device");
        for(const auto& b:buffers) owned(state_,b.state_);
        std::lock_guard lock(state_->mutex);
        require(!state_->stats.in_flight,ErrorCode::busy,"native access requires no unobserved GPU submissions");
        auto context=state_->native->context();
        for(const auto& b:buffers) context.buffers.push_back(b.state_->native->handle());
        return callback(context);
    });
    if(!result) return std::unexpected(result.error());
    return *result;
}
} // namespace ii::gpu
