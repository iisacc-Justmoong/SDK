#include "../../src/WebGPUComponent/WebGPU.h"
#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <map>
#include <thread>

using namespace std::chrono_literals;
using namespace ii::webgpu;

namespace fake {
struct Event { bool ready=false, delivered=false; std::function<void()> callback; };
std::mutex mutex;
std::condition_variable changed;
std::map<std::uint64_t, Event> events;
std::vector<std::pair<std::uint64_t, std::chrono::nanoseconds>> waits;
std::atomic<int> pumps=0;
void reset() { std::lock_guard lock(mutex); events.clear(); waits.clear(); pumps=0; }
void add(std::uint64_t id, std::function<void()> callback) {
    std::lock_guard lock(mutex); events[id].callback=std::move(callback);
}
void signal(std::uint64_t id) {
    { std::lock_guard lock(mutex); events[id].ready=true; }
    changed.notify_all();
}
bool await_calls(std::size_t count) {
    std::unique_lock lock(mutex);
    return changed.wait_for(lock, 3s, [&] { return waits.size()>=count; });
}
}
namespace ii::webgpu::detail {
struct InstanceState {};
void process_events(const std::shared_ptr<InstanceState>&) { ++fake::pumps; }
Status wait_event(const std::shared_ptr<InstanceState>&, WGPUFuture future, std::chrono::nanoseconds timeout) {
    std::unique_lock lock(fake::mutex);
    fake::waits.emplace_back(future.id,timeout);
    fake::changed.notify_all();
    auto& event=fake::events[future.id];
    if (!fake::changed.wait_for(lock, timeout, [&] { return event.ready; }))
        return std::unexpected(Error{ErrorCode::Timeout,"native timeout"});
    if (event.delivered) return std::unexpected(Error{ErrorCode::Internal,"future was waited twice"});
    event.delivered=true;
    auto callback=event.callback;
    lock.unlock();
    if (callback) callback();
    return {};
}
}
namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
auto make_future(const std::shared_ptr<detail::AsyncState<int>>& state) {
    return Future<int>(state,std::make_shared<detail::InstanceState>());
}
void multipleNativeCompletions() {
    fake::reset();
    auto state=std::make_shared<detail::AsyncState<int>>();
    auto remaining=std::make_shared<std::atomic<int>>(3);
    for (std::uint64_t id=1;id<=3;++id) {
        detail::set_future(state,WGPUFuture{id});
        fake::add(id,[state,remaining] { if (--*remaining==0) detail::complete<int>(state,42); });
    }
    auto future=make_future(state);
    Result<int> result=std::unexpected(Error{ErrorCode::Internal,"not run"});
    std::jthread waiter([&] { result=future.wait(2s); });
    const bool started=fake::await_calls(1);
    fake::signal(3); fake::signal(1); fake::signal(2);
    waiter.join();
    check(started && result && *result==42,"all native completions must be observed");
    std::lock_guard lock(fake::mutex);
    check(fake::waits.size()==3,"each native completion must be waited once");
    check(fake::waits[0].first==1 && fake::waits[1].first==2 && fake::waits[2].first==3,"wrong future waited");
    check(fake::waits[0].second>1s,"wait used a short polling interval");
    check(fake::pumps==0,"wait pumped events without an explicit poll");
}
void timeoutAndRetry() {
    fake::reset();
    auto state=std::make_shared<detail::AsyncState<int>>();
    detail::set_future(state,WGPUFuture{7});
    fake::add(7,[state] { detail::complete<int>(state,9); });
    auto future=make_future(state);
    auto timed=future.wait(20ms);
    check(!timed && timed.error().code==ErrorCode::Timeout,"timeout was lost");
    { std::lock_guard lock(fake::mutex); check(fake::waits.size()==1,"timeout retried internally"); }
    fake::signal(7);
    auto result=future.wait(1s);
    check(result && *result==9,"timed-out future was not reusable");
    check(future.poll() && fake::pumps==1,"explicit poll did not process events");
}
void sharedWait() {
    fake::reset();
    auto state=std::make_shared<detail::AsyncState<int>>();
    detail::set_future(state,WGPUFuture{8});
    fake::add(8,[state] { detail::complete<int>(state,11); });
    auto future=make_future(state);
    Result<int> result=std::unexpected(Error{ErrorCode::Internal,"not run"});
    std::jthread waiter([&] { result=future.wait(2s); });
    const bool started=fake::await_calls(1);
    auto short_wait=future.wait(20ms);
    fake::signal(8);
    waiter.join();
    check(started && !short_wait && short_wait.error().code==ErrorCode::Timeout,"independent waiter deadline failed");
    check(result && *result==11,"native waiter lost the completion");
    std::lock_guard lock(fake::mutex);
    check(fake::waits.size()==1,"concurrent waiters duplicated native waits");
}
void emptyAndReady() {
    Future<int> empty;
    auto invalid=empty.wait(0ns);
    check(!invalid && invalid.error().code==ErrorCode::InvalidArgument,"empty future was not rejected");
    auto state=std::make_shared<detail::AsyncState<int>>();
    auto future=make_future(state);
    auto pending=future.wait(0ns);
    check(!pending && pending.error().code==ErrorCode::Timeout,"zero timeout must not block");
    detail::complete<int>(state,4);
    auto ready=future.wait(0ns);
    check(ready && *ready==4,"ready result lost at zero timeout");
}
}
int main() {
    try { multipleNativeCompletions(); timeoutAndRetry(); sharedWait(); emptyAndReady(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
