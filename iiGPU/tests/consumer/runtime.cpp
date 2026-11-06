#include <iiGPU/Completion.h>
#include <iiGPU/Session.h>
#include <memory>
#include <stdexcept>

int main() {
    using namespace ii::gpu;
    bool invalid=false;
    try { Session session(Device{}); } catch(const std::invalid_argument&) { invalid=true; }
    if(!invalid)return 3;
    auto pending=std::make_shared<bool>(true);
    CompletionScheduler::Tick tick;
    CompletionScheduler scheduler{[&](std::chrono::nanoseconds, CompletionScheduler::Tick next) -> Result<CompletionTimer> {
        tick=std::move(next);
        return CompletionTimer{[pending] { *pending=false; }, [pending] { return *pending; }};
    }};
    int calls=0;
    auto watch=CompletionWatch::observe_polling(std::move(scheduler), []() -> Result<bool> { return true; },
        [&](Result<void> result) { if(result) ++calls; }, std::chrono::milliseconds(1));
    if(!watch) return 1;
    tick();
    return calls==1 && !watch->active() ? 0 : 2;
}
