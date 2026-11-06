#include "Session.h"
#include "Backends/Backend.h"
#include <limits>
#include <mutex>

namespace ii::gpu {
using namespace detail;
struct Session::Impl final {
    struct Slot final {std::optional<Dispatch> plan;std::uint64_t generation=0;};
    std::mutex mutex;
    Device device;
    std::vector<Slot> slots;
    std::vector<std::size_t> free;
    SessionStats stats;
    Impl(Device value,std::size_t capacity):device(std::move(value)),slots(capacity){
        free.reserve(capacity);for(std::size_t i=capacity;i;--i)free.push_back(i-1);
        stats.capacity=capacity;
    }
};

Session::Session(Device device,SessionOptions options){
    if(!device||!options.dispatch_capacity)throw std::invalid_argument("session requires a device and positive dispatch capacity");
    impl_=std::make_shared<Impl>(std::move(device),options.dispatch_capacity);
}
Session::~Session(){close();}
Result<DispatchToken> Session::prepare(const ComputePipeline& pipeline,std::span<const Binding> bindings,Extent groups){
    return protect([&] {
        std::lock_guard lock(impl_->mutex);
        require(!impl_->stats.closed,ErrorCode::wrong_state,"GPU session is closed");
        while(!impl_->free.empty()&&impl_->slots[impl_->free.back()].generation==(std::numeric_limits<std::uint64_t>::max)())impl_->free.pop_back();
        require(!impl_->free.empty(),ErrorCode::limit_exceeded,"GPU session dispatch capacity is exhausted");
        auto temporary=impl_->device.commands();
        std::vector<std::byte> constants(pipeline.state_?pipeline.state_->desc.constants_bytes:0);
        auto result=temporary.dispatch(pipeline,bindings,groups,constants);
        if(!result)throw Failure(result.error().code,result.error().message);
        auto plan=std::get<Dispatch>(std::move(temporary.state_->commands.back()));plan.constants.clear();
        const auto index=impl_->free.back();auto& slot=impl_->slots[index];
        slot.plan.emplace(std::move(plan));impl_->free.pop_back();++slot.generation;
        ++impl_->stats.active;++impl_->stats.prepared;
        DispatchToken token;token.owner_=impl_;token.slot_=index;token.generation_=slot.generation;return token;
    });
}
Result<void> Session::dispatch(CommandList& commands,const DispatchToken& token,std::span<const std::byte> constants)const{
    return protect_void([&] {
        std::lock_guard lock(impl_->mutex);
        require(!impl_->stats.closed,ErrorCode::wrong_state,"GPU session is closed");
        const auto owner=token.owner_.lock();
        require(owner&&owner.get()==impl_.get(),ErrorCode::invalid_argument,"dispatch token belongs to another session");
        require(token.slot_<impl_->slots.size(),ErrorCode::wrong_state,"dispatch token is invalid");
        const auto& slot=impl_->slots[token.slot_];
        require(slot.plan&&slot.generation==token.generation_,ErrorCode::wrong_state,"dispatch token is stale");
        auto result=commands.append_prepared(*slot.plan,constants);
        if(!result)throw Failure(result.error().code,result.error().message);
        ++impl_->stats.dispatches;
    });
}
Result<void> Session::release(const DispatchToken& token){
    std::optional<Dispatch> retired;
    return protect_void([&] {
        std::lock_guard lock(impl_->mutex);
        require(!impl_->stats.closed,ErrorCode::wrong_state,"GPU session is closed");
        const auto owner=token.owner_.lock();
        require(owner&&owner.get()==impl_.get(),ErrorCode::invalid_argument,"dispatch token belongs to another session");
        require(token.slot_<impl_->slots.size(),ErrorCode::wrong_state,"dispatch token is invalid");
        auto& slot=impl_->slots[token.slot_];
        require(slot.plan&&slot.generation==token.generation_,ErrorCode::wrong_state,"dispatch token is stale");
        retired.swap(slot.plan);impl_->free.push_back(token.slot_);--impl_->stats.active;++impl_->stats.released;
    });
}
void Session::close(){
    std::vector<Impl::Slot> retired;Device device;
    {
        std::lock_guard lock(impl_->mutex);
        if(impl_->stats.closed)return;
        impl_->stats.closed=true;impl_->stats.active=0;
        retired.swap(impl_->slots);impl_->free.clear();std::swap(device,impl_->device);
    }
}
SessionStats Session::stats()const{std::lock_guard lock(impl_->mutex);return impl_->stats;}
} // namespace ii::gpu
