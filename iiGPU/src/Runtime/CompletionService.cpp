#include "CompletionService.h"
#include <condition_variable>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ii::gpu {
namespace {
thread_local const void* current_service=nullptr;
struct Cleanup final {
    const void* state;
    Cleanup* previous;
    static thread_local Cleanup* current;
    explicit Cleanup(const void* value):state(value),previous(current) { current=this; }
    ~Cleanup() { current=previous; }
    static bool contains(const void* value) {
        for(auto* item=current;item;item=item->previous) if(item->state==value) return true;
        return false;
    }
};
thread_local Cleanup* Cleanup::current=nullptr;
}

namespace detail {
struct CompletionServiceState final {
    enum class Status { free,queued,waiting,delivering,retiring };
    struct Slot final {
        CompletionWait wait;
        CompletionCallback callback;
        std::uint64_t generation=0;
        std::size_t cleanups=0;
        Status status=Status::free;
        bool cancelled=false;
    };
    explicit CompletionServiceState(CompletionServiceOptions options)
        :slots(options.capacity),queue(options.capacity) {
        free.reserve(options.capacity);
        for(std::size_t i=options.capacity;i;--i) free.push_back(i-1);
        statistics.workers=options.workers;
        statistics.capacity=options.capacity;
    }
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::vector<Slot> slots;
    std::vector<std::size_t> free,queue;
    std::size_t head=0,count=0;
    CompletionServiceStats statistics;
    std::exception_ptr failure;

    void run() {
        current_service=this;
        for(;;) {
            std::size_t index;
            CompletionWait wait;
            {
                std::unique_lock lock(mutex);
                changed.wait(lock,[&]{return count || statistics.stopped;});
                if(!count) break;
                index=queue[head]; head=(head+1)%queue.size(); --count;
                auto& slot=slots[index];
                slot.status=Status::waiting;
                slot.wait.swap(wait);
                --statistics.queued; ++statistics.active;
            }
            Result<void> result;
            try { result=wait(); }
            catch(const std::exception& error) { result=std::unexpected(Error{ErrorCode::backend_failure,error.what()}); }
            catch(...) { result=std::unexpected(Error{ErrorCode::backend_failure,"completion wait failed"}); }
            wait={};
            const bool succeeded=bool(result);
            CompletionCallback callback;
            {
                std::lock_guard lock(mutex);
                auto& slot=slots[index];
                slot.status=Status::delivering;
                if(!slot.cancelled) slot.callback.swap(callback);
            }
            std::exception_ptr callback_error;
            try { if(callback) callback(std::move(result)); }
            catch(...) { callback_error=std::current_exception(); }
            callback={};
            {
                std::unique_lock lock(mutex);
                auto& slot=slots[index];
                slot.status=Status::retiring;
                changed.wait(lock,[&]{return slot.cleanups==0;});
                if(succeeded) ++statistics.completed; else ++statistics.failed;
                if(callback_error) {
                    ++statistics.callback_failed;
                    if(!failure) failure=callback_error;
                }
                --statistics.active; --statistics.pending;
                slot.cancelled=false; slot.status=Status::free;
                free.push_back(index);
                changed.notify_all();
            }
        }
        current_service=nullptr;
    }
};
}

CompletionTicket::CompletionTicket(const std::shared_ptr<detail::CompletionServiceState>& state,
                                   std::size_t slot,std::uint64_t generation)
    :state_(state),slot_(slot),generation_(generation) {}
bool CompletionTicket::cancel() const {
    const auto state=state_.lock();
    if(!state) return false;
    CompletionCallback retired;
    {
        std::lock_guard lock(state->mutex);
        if(slot_>=state->slots.size()) return false;
        auto& slot=state->slots[slot_];
        using Status=detail::CompletionServiceState::Status;
        if(slot.generation!=generation_ || (slot.status!=Status::queued && slot.status!=Status::waiting) || slot.cancelled)
            return false;
        slot.cancelled=true;
        ++slot.cleanups; ++state->statistics.cancelled;
        slot.callback.swap(retired);
    }
    { Cleanup cleanup(state.get()); retired={}; }
    {
        std::lock_guard lock(state->mutex);
        --state->slots[slot_].cleanups;
        state->changed.notify_all();
    }
    return true;
}
bool CompletionTicket::pending() const {
    const auto state=state_.lock();
    if(!state) return false;
    std::lock_guard lock(state->mutex);
    return slot_<state->slots.size() && state->slots[slot_].generation==generation_ &&
           state->slots[slot_].status!=detail::CompletionServiceState::Status::free;
}

struct CompletionService::Impl final {
    std::shared_ptr<detail::CompletionServiceState> state;
    std::vector<std::thread> workers;
    std::mutex lifecycle;
    bool joined=false;
    explicit Impl(CompletionServiceOptions options) {
        if(!options.workers || !options.capacity || options.workers>options.capacity)
            throw std::invalid_argument("completion service requires positive bounded workers and capacity");
        state=std::make_shared<detail::CompletionServiceState>(options);
        workers.reserve(options.workers);
        try {
            for(std::size_t i=0;i<options.workers;++i)
                workers.emplace_back([owner=state]{owner->run();});
        } catch(...) {
            { std::lock_guard lock(state->mutex); state->statistics.stopped=true; }
            state->changed.notify_all();
            for(auto& worker:workers) if(worker.joinable()) worker.join();
            throw;
        }
    }
};
CompletionService::CompletionService(CompletionServiceOptions options):impl_(std::make_unique<Impl>(options)) {}
CompletionService::~CompletionService() { if(!close()) std::terminate(); }
Result<CompletionTicket> CompletionService::observe(CompletionWait wait,CompletionCallback callback) {
    if(!wait || !callback) return std::unexpected(Error{ErrorCode::invalid_argument,"completion wait and callback are required"});
    const auto state=impl_->state;
    std::unique_lock lock(state->mutex);
    if(state->statistics.stopped) return std::unexpected(Error{ErrorCode::wrong_state,"completion service is stopped"});
    while(!state->free.empty() && state->slots[state->free.back()].generation==(std::numeric_limits<std::uint64_t>::max)())
        state->free.pop_back();
    if(state->free.empty()) return std::unexpected(Error{ErrorCode::limit_exceeded,"completion service capacity is exhausted"});
    const auto index=state->free.back(); state->free.pop_back();
    auto& slot=state->slots[index];
    slot.wait.swap(wait); slot.callback.swap(callback);
    ++slot.generation; slot.status=detail::CompletionServiceState::Status::queued;
    state->queue[(state->head+state->count)%state->queue.size()]=index; ++state->count;
    ++state->statistics.submitted; ++state->statistics.pending; ++state->statistics.queued;
    CompletionTicket ticket(state,index,slot.generation);
    lock.unlock(); state->changed.notify_one();
    return ticket;
}
Result<CompletionTicket> CompletionService::watch(const Submission& submission,CompletionCallback callback) {
    if(!submission) return std::unexpected(Error{ErrorCode::invalid_argument,"completion service needs a GPU submission"});
    return observe([submission]{return submission.wait();},std::move(callback));
}
void CompletionService::request_stop() {
    const auto state=impl_->state;
    { std::lock_guard lock(state->mutex); state->statistics.stopped=true; }
    state->changed.notify_all();
}
Result<void> CompletionService::close() {
    const auto state=impl_->state;
    if(current_service==state.get() || Cleanup::contains(state.get()))
        return std::unexpected(Error{ErrorCode::busy,"completion service cannot close from its own worker or capture"});
    std::unique_lock lifecycle(impl_->lifecycle,std::try_to_lock);
    if(!lifecycle.owns_lock()) return std::unexpected(Error{ErrorCode::busy,"completion service is already closing"});
    request_stop();
    if(!impl_->joined) {
        try { for(auto& worker:impl_->workers) if(worker.joinable()) worker.join(); }
        catch(const std::system_error& error) { return std::unexpected(Error{ErrorCode::backend_failure,error.what()}); }
        impl_->joined=true;
    }
    return {};
}
CompletionServiceStats CompletionService::stats() const {
    std::lock_guard lock(impl_->state->mutex); return impl_->state->statistics;
}
std::exception_ptr CompletionService::callback_failure() const {
    std::lock_guard lock(impl_->state->mutex); return impl_->state->failure;
}
} // namespace ii::gpu
