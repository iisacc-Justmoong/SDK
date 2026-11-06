#pragma once
#include "Completion.h"
#include <exception>

namespace ii::gpu {
using CompletionWait = std::function<Result<void>()>;
struct CompletionServiceOptions final { std::size_t workers=1,capacity=64; };
struct CompletionServiceStats final {
    std::size_t workers=0,capacity=0,pending=0,queued=0,active=0;
    std::uint64_t submitted=0,completed=0,failed=0,cancelled=0,callback_failed=0;
    bool stopped=false;
};
namespace detail { struct CompletionServiceState; }
class CompletionService;
class CompletionTicket final {
public:
    CompletionTicket()=default;
    // Cancels delivery only; submitted GPU work still runs to completion.
    bool cancel() const;
    [[nodiscard]] bool pending() const;
private:
    friend class CompletionService;
    CompletionTicket(const std::shared_ptr<detail::CompletionServiceState>&,std::size_t,std::uint64_t);
    std::weak_ptr<detail::CompletionServiceState> state_;
    std::size_t slot_=0;
    std::uint64_t generation_=0;
};

// Explicit, bounded native wait workers. A service drains accepted GPU jobs;
// close/destruction may wait for real device completion and must not occur on
// the service's worker or in its retiring captures.
class CompletionService final {
public:
    explicit CompletionService(CompletionServiceOptions={});
    ~CompletionService();
    CompletionService(const CompletionService&)=delete;
    CompletionService& operator=(const CompletionService&)=delete;
    CompletionService(CompletionService&&)=delete;
    CompletionService& operator=(CompletionService&&)=delete;
    [[nodiscard]] Result<CompletionTicket> observe(CompletionWait,CompletionCallback);
    [[nodiscard]] Result<CompletionTicket> watch(const Submission&,CompletionCallback);
    void request_stop();
    [[nodiscard]] Result<void> close();
    [[nodiscard]] CompletionServiceStats stats() const;
    [[nodiscard]] std::exception_ptr callback_failure() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ii::gpu
