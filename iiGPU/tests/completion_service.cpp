#include <iiGPU/CompletionService.h>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <latch>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace ii::gpu;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
struct Gate final {
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t entered=0;
    bool open=false;
    Result<void> wait() {
        std::unique_lock lock(mutex);
        ++entered; changed.notify_all();
        if(!changed.wait_for(lock,5s,[&]{return open;}))
            return std::unexpected(Error{ErrorCode::timeout,"fixture gate timed out"});
        return {};
    }
    bool await(std::size_t count) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock,5s,[&]{return entered>=count;});
    }
    void release() { std::lock_guard lock(mutex); open=true; changed.notify_all(); }
};
struct Release final { Gate& gate; ~Release(){gate.release();} };
void bounded_parallel_and_cancellation() {
    Gate gate;
    CompletionService service({.workers=2,.capacity=2});
    Release release{gate};
    std::atomic<unsigned> callbacks=0;
    std::atomic<bool> off_caller=false;
    const auto caller=std::this_thread::get_id();
    auto resource=std::make_shared<int>(42);
    std::weak_ptr<int> weak=resource;
    auto first=service.observe([&]{return gate.wait();},[resource](Result<void>){});
    resource.reset();
    auto second=service.observe([&]{return gate.wait();},[&](Result<void> result){
        if(result && std::this_thread::get_id()!=caller) off_caller=true;
        ++callbacks;
    });
    const bool entered=gate.await(2);
    const auto busy=service.observe([]()->Result<void>{return {};},[](Result<void>){});
    const auto held=service.stats();
    const bool suppressed=first && first->cancel() && !first->cancel() && weak.expired() && first->pending();
    gate.release();
    const auto closed=service.close();
    const auto final=service.stats();
    check(first && second && entered && !busy && busy.error().code==ErrorCode::limit_exceeded &&
          held.pending==2 && held.active==2 && suppressed,"bounded waiting, parallelism and cancellation");
    check(closed && callbacks==1 && off_caller && final.pending==0 && final.active==0 &&
          final.completed==2 && final.cancelled==1 && !first->pending() && !second->pending(),
          "completion and canceled observations retire exactly once");
}
void failures_and_self_close() {
    CompletionService service({.workers=1,.capacity=4});
    std::atomic<unsigned> errors=0;
    auto first=service.observe([]()->Result<void>{return std::unexpected(Error{ErrorCode::backend_failure,"device"});},
        [&](Result<void> result){if(!result && result.error().code==ErrorCode::backend_failure) ++errors;});
    auto second=service.observe([]()->Result<void>{throw std::runtime_error("wait failed");},
        [&](Result<void> result){if(!result && result.error().code==ErrorCode::backend_failure) ++errors;});
    auto third=service.observe([]()->Result<void>{return {};},[](Result<void>){throw std::runtime_error("callback failed");});
    std::atomic<bool> self_rejected=false;
    auto fourth=service.observe([]()->Result<void>{return {};},[&](Result<void>){
        auto result=service.close(); self_rejected=!result && result.error().code==ErrorCode::busy;
    });
    auto closed=service.close();
    auto stats=service.stats();
    check(first && second && third && fourth && closed && errors==2 && self_rejected &&
          stats.failed==2 && stats.completed==2 && stats.callback_failed==1 && service.callback_failure(),
          "wait and callback failures remain observable and the worker continues");
    check(!service.observe([]()->Result<void>{return {};},[](Result<void>){}),"closed service rejects admission");
}
void capture_cleanup_barrier() {
    Gate waiting;
    CompletionService service({.workers=1,.capacity=1});
    Release release{waiting};
    std::latch destroying(1),proceed(1);
    auto resource=std::shared_ptr<int>(new int(1),[&](int* value){destroying.count_down(); proceed.wait(); delete value;});
    auto ticket=service.observe([&]{return waiting.wait();},[resource](Result<void>){});
    resource.reset();
    const bool entered=waiting.await(1);
    std::thread canceller([&]{if(ticket) (void)ticket->cancel();});
    destroying.wait();
    waiting.release();
    const auto extra=service.observe([]()->Result<void>{return {};},[](Result<void>){});
    const bool retained=ticket && ticket->pending() && !extra && extra.error().code==ErrorCode::limit_exceeded;
    proceed.count_down(); canceller.join();
    check(entered && retained && service.close(),"slot is held until canceled capture destruction completes");
}
void invalid_options() {
    bool rejected=false;
    try { CompletionService invalid({.workers=2,.capacity=1}); } catch(const std::invalid_argument&) { rejected=true; }
    CompletionService service;
    check(rejected && !service.observe({},[](Result<void>) {}) &&
          !service.watch(Submission{},[](Result<void>) {}) && service.close(),"invalid admission and options are rejected");
}
void capture_reentry_and_expired_token() {
    CompletionTicket surviving;
    Gate waiting;
    bool guarded=false;
    {
        CompletionService service({.workers=1,.capacity=1});
        Release release{waiting};
        struct Capture final {
            CompletionService& service; bool& guarded;
            ~Capture() { auto result=service.close(); guarded=!result && result.error().code==ErrorCode::busy; }
        };
        auto capture=std::shared_ptr<Capture>(new Capture{service,guarded});
        auto ticket=service.observe([&]{return waiting.wait();},[capture](Result<void>){});
        capture.reset();
        const bool entered=waiting.await(1);
        const bool canceled=ticket && ticket->cancel();
        if(ticket) surviving=*ticket;
        waiting.release();
        const auto closed=service.close();
        check(entered && canceled && guarded && closed,"capture reentry cannot self-deadlock service shutdown");
    }
    check(!surviving.pending() && !surviving.cancel(),"ticket remains safe after service destruction");
}
}
int main() {
    try { bounded_parallel_and_cancellation(); failures_and_self_close(); capture_cleanup_barrier(); invalid_options(); capture_reentry_and_expired_token(); }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
