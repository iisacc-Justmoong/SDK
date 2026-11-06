#pragma once
#include "WebWait.h"
#include <iiSDKCore/Process/Process.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace iiGlobalSearch::detail {
// Completion is observed by one blocked native wait. Destruction cancels the
// owned process tree and joins that waiter before the wake gate can disappear.
class ProcessCompletion final {
public:
    ProcessCompletion(ii::process::Process& process, WebWait& gate, std::chrono::milliseconds timeout)
        : process_(process), worker_([this,&gate,timeout] {
              try {
                  auto result=process_.wait_for(timeout);
                  exited_.store(!result || result->has_value());
              } catch (...) { exited_.store(true); }
              gate.signal();
          }) {}
    ~ProcessCompletion() {
        try { (void)process_.cancel(); } catch (...) {}
        if(worker_.joinable()) worker_.join();
    }
    ProcessCompletion(const ProcessCompletion&)=delete;
    ProcessCompletion& operator=(const ProcessCompletion&)=delete;
    [[nodiscard]] bool exited() const noexcept { return exited_.load(); }
private:
    ii::process::Process& process_;
    std::atomic<bool> exited_{false};
    std::thread worker_;
};
}
