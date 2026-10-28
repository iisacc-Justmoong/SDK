#include <iiGPU/Completion.h>
#include <memory>

int main() {
    using namespace ii::gpu;
    auto pending=std::make_shared<bool>(true);
    CompletionScheduler::Tick tick;
    CompletionScheduler scheduler{[&](std::chrono::nanoseconds, CompletionScheduler::Tick next) -> Result<CompletionTimer> {
        tick=std::move(next);
        return CompletionTimer{[pending] { *pending=false; }, [pending] { return *pending; }};
    }};
    int calls=0;
    auto watch=CompletionWatch::observe(std::move(scheduler), []() -> Result<bool> { return true; },
        [&](Result<void> result) { if(result) ++calls; });
    if(!watch) return 1;
    tick();
    return calls==1 && !watch->active() ? 0 : 2;
}
