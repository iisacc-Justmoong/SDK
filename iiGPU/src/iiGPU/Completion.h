#pragma once
#include "GPU.h"
#include <iiSDKCore/Runtime/EventLoop.h>

namespace ii::gpu {
// Completion observation shares the caller's bounded EventLoop. It creates no
// worker thread. Readiness polling is nonblocking; releasing the last GPU
// Submission handle still follows its explicit resource-lifetime contract.
class CompletionWatch final {
public:
    CompletionWatch()=default;
    ~CompletionWatch();
    CompletionWatch(CompletionWatch&&) noexcept;
    CompletionWatch& operator=(CompletionWatch&&) noexcept;
    CompletionWatch(const CompletionWatch&)=delete;
    CompletionWatch& operator=(const CompletionWatch&)=delete;
    static Result<CompletionWatch> create(runtime::EventLoop&,Submission,
        std::function<void(Result<void>)>,std::chrono::nanoseconds interval=std::chrono::milliseconds(1));
    void cancel(); // Cancels observation; submitted GPU work continues.
    bool active() const;
private:
    struct State;
    explicit CompletionWatch(std::shared_ptr<State> state):state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};
} // namespace ii::gpu
