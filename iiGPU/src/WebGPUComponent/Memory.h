#pragma once

#include "Result.h"
#include <cstdint>
#include <memory>

namespace ii::webgpu {
namespace detail { struct BudgetState; struct BudgetLease; struct RangeState; struct RangeLease; }
struct MemoryStats final {
    std::uint64_t limit = 0, used = 0, peak = 0, allocations = 0;
};
class Reservation final {
public:
    Reservation() = default;
    [[nodiscard]] std::uint64_t size() const noexcept;
private:
    explicit Reservation(std::shared_ptr<detail::BudgetLease> lease) : lease_(std::move(lease)) {}
    std::shared_ptr<detail::BudgetLease> lease_;
    friend class MemoryBudget;
};
class MemoryBudget final {
public:
    explicit MemoryBudget(std::uint64_t limit);
    [[nodiscard]] Result<Reservation> reserve(std::uint64_t bytes) const;
    [[nodiscard]] Status set_limit(std::uint64_t bytes) const;
    [[nodiscard]] MemoryStats stats() const;
private:
    std::shared_ptr<detail::BudgetState> state_;
};
struct RangeStats final {
    std::uint64_t capacity = 0, used = 0, largest_free = 0, allocations = 0;
};
class RangeAllocation final {
public:
    RangeAllocation() = default;
    [[nodiscard]] std::uint64_t offset() const noexcept;
    [[nodiscard]] std::uint64_t size() const noexcept;
private:
    explicit RangeAllocation(std::shared_ptr<detail::RangeLease> lease) : lease_(std::move(lease)) {}
    std::shared_ptr<detail::RangeLease> lease_;
    friend class RangeAllocator;
};
class RangeAllocator final {
public:
    explicit RangeAllocator(std::uint64_t capacity);
    [[nodiscard]] Result<RangeAllocation> allocate(std::uint64_t bytes, std::uint64_t alignment = 4) const;
    [[nodiscard]] RangeStats stats() const;
private:
    std::shared_ptr<detail::RangeState> state_;
};
}
