#include <iiGPU/Completion.h>
#include <chrono>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace ii::gpu;
using namespace std::chrono_literals;
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct TimerState {
    bool pending = true, reenter_on_cancel = false;
    unsigned cancellations = 0;
    std::function<void()> tick;
};
struct Token {
    std::weak_ptr<TimerState> state;
    bool cancel() const {
        auto timer = state.lock();
        if (!timer) return false;
        bool previous = timer->pending;
        timer->pending = false;
        ++timer->cancellations;
        if (timer->reenter_on_cancel && timer->tick) timer->tick();
        return previous;
    }
    bool pending() const { auto timer=state.lock(); return timer && timer->pending; }
};
struct Admission {
    Token token;
    bool accepted = false;
    explicit operator bool() const { return accepted; }
};
struct FakeLoop {
    bool reject = false, inline_tick = false, reenter_on_cancel = false;
    std::shared_ptr<TimerState> timer;
    Admission every(std::chrono::nanoseconds, std::function<void()> callback) {
        if (reject) return {};
        timer=std::make_shared<TimerState>();
        timer->tick=std::move(callback);
        timer->reenter_on_cancel=reenter_on_cancel;
        if (inline_tick) timer->tick();
        return {Token{timer}, true};
    }
    void tick() { if (timer && timer->pending) timer->tick(); }
};
struct ThrowingCopyToken {
    std::shared_ptr<TimerState> state;
    explicit ThrowingCopyToken(std::shared_ptr<TimerState> value):state(std::move(value)) {}
    ThrowingCopyToken(const ThrowingCopyToken&) { throw std::runtime_error("token copy failed"); }
    ThrowingCopyToken(ThrowingCopyToken&&) noexcept = default;
    bool cancel() { state->pending=false; return true; }
    bool pending() const { return state->pending; }
};
struct ThrowingCopyAdmission {
    ThrowingCopyToken token;
    explicit operator bool() const { return true; }
};
struct ThrowingCopyLoop {
    std::shared_ptr<TimerState> timer = std::make_shared<TimerState>();
    ThrowingCopyAdmission every(std::chrono::nanoseconds, std::function<void()> tick) {
        timer->tick=std::move(tick);
        return {ThrowingCopyToken{timer}};
    }
};
void implicitPollingIsDisabled() {
    FakeLoop loop;
    auto watch=CompletionWatch::observe(loop, []() -> Result<bool> { return false; }, [](Result<void>) {});
    check(!watch && watch.error().code==ErrorCode::unsupported, "implicit polling was admitted");
    check(!loop.timer, "implicit polling scheduled a timer");
    auto legacy=CompletionWatch::observe(loop, []() -> Result<bool> { return false; }, [](Result<void>) {}, 2ms);
    check(!legacy && legacy.error().code==ErrorCode::unsupported && !loop.timer,
          "legacy polling entry point must not schedule an implicit timer");
    unsigned scheduled=0;
    CompletionScheduler scheduler{[&](std::chrono::nanoseconds,CompletionScheduler::Tick) -> Result<CompletionTimer> {
        ++scheduled; return std::unexpected(Error{ErrorCode::backend_failure,"unexpected scheduling"});
    }};
    auto direct=CompletionWatch::observe(std::move(scheduler), []() -> Result<bool> { return false; }, [](Result<void>) {});
    check(!direct && direct.error().code==ErrorCode::unsupported && scheduled==0,
          "legacy scheduler entry point started polling");
}
template<class Scheduler>
Result<CompletionWatch> requestedPolling(Scheduler&& scheduler, CompletionProbe probe, CompletionCallback callback,
                                         std::chrono::nanoseconds interval=1ms) {
    return CompletionWatch::observe_polling(std::forward<Scheduler>(scheduler),std::move(probe),std::move(callback),interval);
}
void delayedCompletion() {
    FakeLoop loop; unsigned polls=0, calls=0;
    auto watch=requestedPolling(loop, [&]() -> Result<bool> { return ++polls == 3; },
        [&](Result<void> result) { check(bool(result), "completion error"); ++calls; });
    check(bool(watch) && watch->active(), "watch was not admitted");
    loop.tick(); loop.tick();
    check(calls==0 && watch->active(), "early completion");
    loop.tick(); loop.tick();
    check(calls==1 && !watch->active() && !loop.timer->pending, "completion must be once");
}
void errorAndCancellation() {
    FakeLoop loop; unsigned calls=0;
    auto watch=requestedPolling(loop, []() -> Result<bool> {
        return std::unexpected(Error{ErrorCode::backend_failure,"device lost"});
    }, [&](Result<void> result) { check(!result && result.error().code==ErrorCode::backend_failure, "error lost"); ++calls; });
    check(bool(watch), "error watch not admitted"); loop.tick(); loop.tick();
    check(calls==1 && !watch->active(), "error completion must be once");
    FakeLoop cancelled; cancelled.reenter_on_cancel=true;
    auto stopped=requestedPolling(cancelled, []() -> Result<bool> { return true; },
        [&](Result<void>) { ++calls; });
    check(bool(stopped), "cancel watch not admitted");
    stopped->cancel(); cancelled.tick();
    check(!stopped->active() && calls==1, "cancel delivered a callback");
    stopped->cancel();
    check(cancelled.timer->cancellations==1, "timer cancelled more than once");
}
void inlineAndRejected() {
    FakeLoop inlineLoop; inlineLoop.inline_tick=true; unsigned calls=0;
    auto watch=requestedPolling(inlineLoop, []() -> Result<bool> { return true; },
        [&](Result<void> result) { check(bool(result), "inline error"); ++calls; });
    check(bool(watch) && calls==1 && !watch->active() && !inlineLoop.timer->pending,
        "inline dispatch must cancel its returned timer");
    FakeLoop full; full.reject=true;
    auto rejected=requestedPolling(full, []() -> Result<bool> { return true; }, [](Result<void>) {});
    check(!rejected && rejected.error().code==ErrorCode::limit_exceeded, "capacity error lost");
    FakeLoop empty;
    auto invalid=requestedPolling(empty, []() -> Result<bool> { return true; }, [](Result<void>) {}, 0ns);
    check(!invalid && invalid.error().code==ErrorCode::invalid_argument, "invalid interval accepted");
    unsigned cancellations=0;
    CompletionScheduler incomplete{[&](std::chrono::nanoseconds,CompletionScheduler::Tick) -> Result<CompletionTimer> {
        return CompletionTimer{[&] { ++cancellations; },{}};
    }};
    auto missing=requestedPolling(std::move(incomplete), []() -> Result<bool> { return true; }, [](Result<void>) {});
    check(!missing && missing.error().code==ErrorCode::invalid_argument && cancellations==1,
        "incomplete scheduler token was not rejected and cancelled");
    CompletionScheduler throwing{[](std::chrono::nanoseconds,CompletionScheduler::Tick) -> Result<CompletionTimer> {
        throw std::runtime_error("scheduler failure");
    }};
    auto failure=requestedPolling(std::move(throwing), []() -> Result<bool> { return true; }, [](Result<void>) {});
    check(!failure && failure.error().code==ErrorCode::backend_failure, "scheduler exception lost");
    ThrowingCopyLoop copyFailure;
    auto copy=requestedPolling(copyFailure, []() -> Result<bool> { return true; }, [](Result<void>) {});
    check(!copy && copy.error().code==ErrorCode::backend_failure && !copyFailure.timer->pending,
        "failed token adaptation left an admitted timer running");
}
void lifetimeAndExceptions() {
    FakeLoop loop; unsigned calls=0;
    {
        auto watch=requestedPolling(loop, []() -> Result<bool> { return false; }, [&](Result<void>) { ++calls; });
        check(bool(watch), "lifetime watch not admitted");
    }
    loop.tick(); check(calls==0 && !loop.timer->pending, "destruction did not cancel observation");
    FakeLoop errors;
    auto watch=requestedPolling(errors, []() -> Result<bool> { throw std::runtime_error("probe failure"); },
        [&](Result<void> result) { check(!result && result.error().code==ErrorCode::backend_failure, "probe exception lost"); ++calls; });
    check(bool(watch), "exception watch not admitted"); errors.tick();
    check(calls==1 && !watch->active(), "exception did not complete");
}
void reentrantProbe() {
    FakeLoop loop; unsigned calls=0; CompletionWatch* self=nullptr;
    auto watch=requestedPolling(loop, [&]() -> Result<bool> { self->cancel(); return true; },
        [&](Result<void>) { ++calls; });
    check(bool(watch), "reentrant watch not admitted");
    self=&*watch; loop.tick();
    check(!watch->active() && calls==0, "reentrant cancellation delivered completion");
}
void cancelWhilePolling() {
    std::mutex mutex; std::condition_variable changed; bool entered=false, release=false;
    std::atomic<bool> pending=true; std::atomic<unsigned> calls=0;
    CompletionScheduler::Tick tick;
    CompletionScheduler scheduler{[&](std::chrono::nanoseconds,CompletionScheduler::Tick next) -> Result<CompletionTimer> {
        tick=std::move(next);
        return CompletionTimer{[&] { pending=false; },[&] { return pending.load(); }};
    }};
    auto watch=requestedPolling(std::move(scheduler),[&]() -> Result<bool> {
        std::unique_lock lock(mutex); entered=true; changed.notify_all();
        changed.wait(lock,[&] { return release; }); return true;
    },[&](Result<void>) { ++calls; });
    check(bool(watch), "concurrent watch not admitted");
    std::jthread worker([&] { tick(); });
    struct Release final { std::mutex& mutex; std::condition_variable& changed; bool& release;
        ~Release() { { std::lock_guard lock(mutex); release=true; } changed.notify_all(); }
    } ensureRelease{mutex,changed,release};
    { std::unique_lock lock(mutex); check(changed.wait_for(lock,5s,[&] { return entered; }), "probe did not start"); }
    watch->cancel();
    { std::lock_guard lock(mutex); release=true; } changed.notify_all();
    worker.join();
    check(!watch->active() && !pending && calls==0, "cancel failed while polling");
}
}
int main() {
    try { implicitPollingIsDisabled(); delayedCompletion(); errorAndCancellation(); inlineAndRejected(); lifetimeAndExceptions(); reentrantProbe(); cancelWhilePolling(); }
    catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
