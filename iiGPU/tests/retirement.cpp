#include "../src/Backends/Backend.h"
#include <iostream>

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
}
int main() {
    try {
        auto device=std::make_shared<DeviceState>();
        std::vector<std::shared_ptr<SubmissionState>> retained;
        unsigned destroyed=0;
        for(unsigned i=0;i<128;++i) {
            auto job=std::make_shared<SubmissionState>();job->owner=device;job->self=job;
            job->native=std::make_unique<Completion>(destroyed,i%2!=0);
            device->jobs.push_back(job);++device->stats.in_flight;
            auto result=job->wait(std::chrono::nanoseconds::zero());
            check(i%2?!result:result&&*result);
            check(job->retired&&!job->native&&device->jobs.empty());
            check(destroyed==i+1&&device->stats.in_flight==0);
            auto again=job->wait(std::chrono::nanoseconds::zero());
            check(i%2?!again:again&&*again);
            retained.push_back(std::move(job));
        }
        check(device->stats.completed==64&&device->stats.failed==64&&destroyed==128);
        std::cout<<"retained_handles=128 native_jobs=0 tracker_entries=0 success_and_error=true\n";
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
