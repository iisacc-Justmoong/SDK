#include "Completion.h"
#include <exception>
#include <mutex>

namespace ii::gpu {
struct CompletionWatch::State final {
    std::mutex mutex;
    CompletionProbe probe;
    CompletionCallback callback;
    std::shared_ptr<CompletionTimer> timer;
    bool active=true, polling=false;
};
namespace {
void cancelTimer(const std::shared_ptr<CompletionTimer>& timer) noexcept {
    if(timer && timer->cancel) {
        try { timer->cancel(); } catch(...) { /* Cancellation must not escape destruction. */ }
    }
}
}
CompletionWatch::~CompletionWatch() { cancel(); }
CompletionWatch::CompletionWatch(CompletionWatch&& other) noexcept:state_(std::move(other.state_)) {}
CompletionWatch& CompletionWatch::operator=(CompletionWatch&& other) noexcept {
    if(this!=&other) {cancel();state_=std::move(other.state_);} return *this;
}
Result<CompletionWatch> CompletionWatch::create(CompletionScheduler scheduler,Submission job,
    CompletionCallback callback,std::chrono::nanoseconds interval) {
    if(!job) return std::unexpected(Error{ErrorCode::invalid_argument,"completion watch needs a job"});
    return observe(std::move(scheduler),[job=std::move(job)] { return job.poll(); },std::move(callback),interval);
}
Result<CompletionWatch> CompletionWatch::observe(CompletionScheduler scheduler,CompletionProbe probe,
    CompletionCallback callback,std::chrono::nanoseconds interval) {
    if(!scheduler.every || !probe || !callback || interval.count()<=0)
        return std::unexpected(Error{ErrorCode::invalid_argument,"completion watch needs a scheduler, probe, callback and positive interval"});
    auto state=std::make_shared<State>(); state->probe=std::move(probe); state->callback=std::move(callback);
    const std::weak_ptr<State> weak=state;
    auto tick=[weak] {
        auto s=weak.lock(); if(!s) return;
        {
            std::lock_guard lock(s->mutex);
            if(!s->active || s->polling) return;
            s->polling=true;
        }
        Result<bool> done;
        try { done=s->probe(); }
        catch(const std::exception& e) { done=std::unexpected(Error{ErrorCode::backend_failure,e.what()}); }
        catch(...) { done=std::unexpected(Error{ErrorCode::backend_failure,"completion probe failed"}); }
        CompletionCallback completion;
        Result<void> result;
        std::shared_ptr<CompletionTimer> timer;
        {
            std::lock_guard lock(s->mutex);
            s->polling=false;
            if(!s->active || (done && !*done)) return;
            if(!done) result=std::unexpected(done.error());
            s->active=false; timer=std::move(s->timer); completion=std::move(s->callback);
        }
        cancelTimer(timer);
        if(completion) completion(std::move(result));
    };
    Result<CompletionTimer> scheduled=std::unexpected(Error{ErrorCode::backend_failure,"completion scheduler failed"});
    try { scheduled=scheduler.every(interval,std::move(tick)); }
    catch(const std::exception& e) { return std::unexpected(Error{ErrorCode::backend_failure,e.what()}); }
    catch(...) { return std::unexpected(Error{ErrorCode::backend_failure,"completion scheduler failed"}); }
    if(!scheduled) return std::unexpected(scheduled.error());
    if(!scheduled->cancel || !scheduled->pending) {
        if(scheduled->cancel) { try { scheduled->cancel(); } catch(...) {} }
        return std::unexpected(Error{ErrorCode::invalid_argument,"scheduler returned an incomplete timer"});
    }
    std::shared_ptr<CompletionTimer> timer;
    try { timer=std::make_shared<CompletionTimer>(std::move(*scheduled)); }
    catch(...) { if(scheduled->cancel) { try { scheduled->cancel(); } catch(...) {} } throw; }
    bool finished=false;
    { std::lock_guard lock(state->mutex); finished=!state->active; if(!finished) state->timer=timer; }
    if(finished) cancelTimer(timer);
    return CompletionWatch(std::move(state));
}
void CompletionWatch::cancel() {
    if(!state_) return;
    std::shared_ptr<CompletionTimer> timer;
    { std::lock_guard lock(state_->mutex); state_->active=false; timer=std::move(state_->timer); }
    cancelTimer(timer);
}
bool CompletionWatch::active() const {
    if(!state_) return false;
    std::shared_ptr<CompletionTimer> timer;
    { std::lock_guard lock(state_->mutex); if(!state_->active) return false; timer=state_->timer; }
    if(!timer || !timer->pending) return false;
    try { return timer->pending(); } catch(...) { return false; }
}
} // namespace ii::gpu
