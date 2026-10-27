#include "Completion.h"
#include <mutex>

namespace ii::gpu {
struct CompletionWatch::State final {
    std::mutex mutex;
    Submission job;
    std::function<void(Result<void>)> callback;
    runtime::TaskToken token;
    bool active=true;
};
CompletionWatch::~CompletionWatch() { cancel(); }
CompletionWatch::CompletionWatch(CompletionWatch&& other) noexcept:state_(std::move(other.state_)) {}
CompletionWatch& CompletionWatch::operator=(CompletionWatch&& other) noexcept {
    if(this!=&other) {cancel();state_=std::move(other.state_);} return *this;
}
Result<CompletionWatch> CompletionWatch::create(runtime::EventLoop& loop,Submission job,
    std::function<void(Result<void>)> callback,std::chrono::nanoseconds interval) {
    if(!job||!callback||interval.count()<=0)
        return std::unexpected(Error{ErrorCode::invalid_argument,"completion watch needs a job, callback and positive interval"});
    auto state=std::make_shared<State>(); state->job=std::move(job); state->callback=std::move(callback);
    const std::weak_ptr<State> weak=state;
    auto scheduled=loop.every(interval,[weak] {
        auto s=weak.lock(); if(!s) return;
        std::function<void(Result<void>)> completion;
        Result<void> result;
        runtime::TaskToken token;
        {
            std::lock_guard lock(s->mutex);
            if(!s->active) return;
            auto done=s->job.poll();
            if(done&&!*done) return;
            if(!done) result=std::unexpected(done.error());
            s->active=false; token=s->token; completion=std::move(s->callback);
        }
        token.cancel(); completion(std::move(result));
    });
    if(!scheduled) return std::unexpected(Error{ErrorCode::limit_exceeded,"Core EventLoop rejected completion observation"});
    {
        std::lock_guard lock(state->mutex);
        state->token=scheduled.token;
        if(!state->active) scheduled.token.cancel();
    }
    return CompletionWatch(std::move(state));
}
void CompletionWatch::cancel() {
    if(!state_) return;
    runtime::TaskToken token;
    { std::lock_guard lock(state_->mutex); state_->active=false; token=state_->token; }
    token.cancel();
}
bool CompletionWatch::active() const {
    if(!state_) return false;
    std::lock_guard lock(state_->mutex);
    return state_->active&&state_->token.pending();
}
} // namespace ii::gpu
