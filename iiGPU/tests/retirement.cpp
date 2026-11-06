#include "../src/Backends/Backend.h"
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <thread>

using namespace ii::gpu;
using namespace ii::gpu::detail;
namespace {
struct Completion final : NativeSubmission {
    unsigned& destroyed;
    bool fail;
    Completion(unsigned& n,bool error):destroyed(n),fail(error) {}
    ~Completion() {++destroyed;}
    bool wait(std::chrono::nanoseconds) override {
        if(fail) throw Failure(ErrorCode::backend_failure,"injected device loss");
        return true;
    }
};
void check(bool v) {if(!v) throw std::runtime_error("completion resource retirement failed");}
struct Signal final {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered=false,release=false,polled=false;
};
struct BlockingCompletion final : NativeSubmission {
    std::shared_ptr<Signal> signal;
    explicit BlockingCompletion(std::shared_ptr<Signal> value):signal(std::move(value)) {}
    bool wait(std::chrono::nanoseconds timeout) override {
        std::unique_lock lock(signal->mutex);
        if(timeout==std::chrono::nanoseconds::zero()) return signal->release;
        signal->entered=true; signal->changed.notify_all();
        signal->changed.wait(lock,[&]{return signal->release;});
        return true;
    }
};
void independent_wait_and_poll() {
    auto device=std::make_shared<DeviceState>();
    auto signal=std::make_shared<Signal>();
    auto job=std::make_shared<SubmissionState>();
    job->owner=device; job->native=std::make_unique<BlockingCompletion>(signal);
    device->jobs.emplace(job.get(),job); ++device->stats.in_flight;
    std::atomic<bool> waiter_ok=false,probe_ok=false;
    std::thread waiter([&]{const auto result=job->wait(std::chrono::nanoseconds::max()); waiter_ok=result && *result;});
    {
        std::unique_lock lock(signal->mutex);
        signal->changed.wait(lock,[&]{return signal->entered;});
    }
    std::thread probe([&]{
        const auto result=job->wait(std::chrono::nanoseconds::zero());
        probe_ok=result && !*result;
        {std::lock_guard lock(signal->mutex); signal->polled=true;}
        signal->changed.notify_all();
    });
    bool prompt=false;
    {
        std::unique_lock lock(signal->mutex);
        prompt=signal->changed.wait_for(lock,std::chrono::seconds(1),[&]{return signal->polled;});
        signal->release=true;
    }
    signal->changed.notify_all();
    waiter.join(); probe.join();
    check(prompt && probe_ok && waiter_ok && device->stats.completed==1 && device->stats.in_flight==0);
}
struct CompletingProbe final : NativeSubmission {
    std::shared_ptr<Signal> signal;
    std::atomic<unsigned>& destroyed;
    CompletingProbe(std::shared_ptr<Signal> value,std::atomic<unsigned>& count):signal(std::move(value)),destroyed(count) {}
    ~CompletingProbe() override {++destroyed;}
    bool wait(std::chrono::nanoseconds timeout) override {
        if(timeout==std::chrono::nanoseconds::zero()) return true;
        std::unique_lock lock(signal->mutex);
        signal->entered=true; signal->changed.notify_all();
        signal->changed.wait(lock,[&]{return signal->release;});
        return true;
    }
};
void concurrent_retirement_preserves_waiter() {
    auto device=std::make_shared<DeviceState>();
    auto signal=std::make_shared<Signal>();
    std::atomic<unsigned> destroyed=0;
    auto job=std::make_shared<SubmissionState>();
    job->owner=device;
    job->native=std::make_shared<CompletingProbe>(signal,destroyed);
    device->jobs.emplace(job.get(),job); ++device->stats.in_flight;
    std::atomic<bool> done=false;
    std::thread waiter([&]{auto result=job->wait(std::chrono::nanoseconds::max()); done=result && *result;});
    {
        std::unique_lock lock(signal->mutex);
        signal->changed.wait(lock,[&]{return signal->entered;});
    }
    const auto probe=job->wait(std::chrono::nanoseconds::zero());
    const bool retained=probe && *probe && job->retired && destroyed==0;
    {std::lock_guard lock(signal->mutex); signal->release=true;}
    signal->changed.notify_all(); waiter.join();
    check(retained && done && destroyed==1 && device->stats.completed==1 && device->stats.in_flight==0);
}
}
int main() {
    try {
        auto device=std::make_shared<DeviceState>();
        std::vector<std::shared_ptr<SubmissionState>> retained;
        unsigned destroyed=0;
        for(unsigned i=0;i<128;++i) {
            auto job=std::make_shared<SubmissionState>();job->owner=device;
            job->native=std::make_unique<Completion>(destroyed,i%2!=0);
            device->jobs.emplace(job.get(),job);++device->stats.in_flight;
            auto result=job->wait(std::chrono::nanoseconds::zero());
            check(i%2?!result:result&&*result);
            check(job->retired&&!job->native&&device->jobs.empty());
            check(destroyed==i+1&&device->stats.in_flight==0);
            auto again=job->wait(std::chrono::nanoseconds::zero());
            check(i%2?!again:again&&*again);
            retained.push_back(std::move(job));
        }
        check(device->stats.completed==64&&device->stats.failed==64&&destroyed==128);
        independent_wait_and_poll();
        concurrent_retirement_preserves_waiter();
        std::cout<<"retained_handles=128 native_jobs=0 tracker_entries=0 success_and_error=true\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
