#pragma once
#include "GPU.h"
#include <concepts>
#include <type_traits>

namespace ii::gpu {
// The caller supplies scheduling and owns its execution context. A timer token
// must provide idempotent cancellation and a thread-safe pending query.
struct CompletionTimer final {
    std::function<void()> cancel;
    std::function<bool()> pending;
};
struct CompletionScheduler final {
    using Tick = std::function<void()>;
    std::function<Result<CompletionTimer>(std::chrono::nanoseconds, Tick)> every;
};
using CompletionProbe = std::function<Result<bool>()>;
using CompletionCallback = std::function<void(Result<void>)>;

template<class Loop>
concept CompletionLoop = requires(Loop& loop, std::chrono::nanoseconds duration, std::function<void()> tick) {
    { static_cast<bool>(loop.every(duration, tick)) } -> std::same_as<bool>;
    loop.every(duration, tick).token.cancel();
    { loop.every(duration, tick).token.pending() } -> std::convertible_to<bool>;
    requires std::copy_constructible<std::remove_cvref_t<decltype(loop.every(duration, tick).token)>>;
};

// Completion observation creates no worker. Cancellation stops observation,
// not submitted GPU work. Callers retain their executor and GPU job lifetimes.
class CompletionWatch final {
public:
    CompletionWatch()=default;
    ~CompletionWatch();
    CompletionWatch(CompletionWatch&&) noexcept;
    CompletionWatch& operator=(CompletionWatch&&) noexcept;
    CompletionWatch(const CompletionWatch&)=delete;
    CompletionWatch& operator=(const CompletionWatch&)=delete;

    static Result<CompletionWatch> observe(CompletionScheduler, CompletionProbe, CompletionCallback,
        std::chrono::nanoseconds interval=std::chrono::milliseconds(1));
    static Result<CompletionWatch> create(CompletionScheduler, Submission, CompletionCallback,
        std::chrono::nanoseconds interval=std::chrono::milliseconds(1));

    // Adapts any caller-owned loop with every(duration, callback), a boolean
    // admission result and a copyable token exposing cancel() and pending().
    // No particular SDK or runtime type is referenced by this module.
    template<CompletionLoop Loop>
    static Result<CompletionWatch> observe(Loop& loop, CompletionProbe probe, CompletionCallback callback,
        std::chrono::nanoseconds interval=std::chrono::milliseconds(1)) {
        CompletionScheduler scheduler{[&loop](std::chrono::nanoseconds duration,
                                              std::function<void()> tick) -> Result<CompletionTimer> {
            auto submission=loop.every(duration, std::move(tick));
            if(!submission)
                return std::unexpected(Error{ErrorCode::limit_exceeded,"completion scheduler rejected observation"});
            try {
                auto token=submission.token;
                return CompletionTimer{
                    [token]() mutable { (void)token.cancel(); },
                    [token]() mutable { return bool(token.pending()); }};
            } catch(...) {
                try { (void)submission.token.cancel(); } catch(...) {}
                throw;
            }
        }};
        return observe(std::move(scheduler),std::move(probe),std::move(callback),interval);
    }

    template<CompletionLoop Loop>
    static Result<CompletionWatch> create(Loop& loop, Submission job, CompletionCallback callback,
        std::chrono::nanoseconds interval=std::chrono::milliseconds(1)) {
        if(!job) return std::unexpected(Error{ErrorCode::invalid_argument,"completion watch needs a job"});
        return observe(loop,[job=std::move(job)] { return job.poll(); },std::move(callback),interval);
    }
    void cancel();
    [[nodiscard]] bool active() const;
private:
    struct State;
    explicit CompletionWatch(std::shared_ptr<State> state):state_(std::move(state)) {}
    std::shared_ptr<State> state_;
};
} // namespace ii::gpu
