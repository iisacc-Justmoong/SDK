#include "Memory.h"
#include <algorithm>
#include <bit>
#include <list>
#include <mutex>

namespace ii::webgpu::detail {
struct BudgetState final {
    mutable std::mutex mutex;
    MemoryStats stats;
};
struct BudgetLease final {
    std::shared_ptr<BudgetState> state;
    std::uint64_t bytes = 0;
    bool active = false;
    ~BudgetLease() {
        if (!active) return;
        std::lock_guard lock(state->mutex);
        state->stats.used -= bytes;
        --state->stats.allocations;
    }
};
struct Block final { std::uint64_t offset, size; bool free; };
struct RangeState final {
    mutable std::mutex mutex;
    std::list<Block> blocks;
    std::uint64_t capacity = 0, used = 0, allocations = 0;
};
struct RangeLease final {
    std::shared_ptr<RangeState> state;
    std::list<Block>::iterator block;
    std::uint64_t offset = 0, size = 0;
    bool active = false;
    ~RangeLease() {
        if (!active) return;
        std::lock_guard lock(state->mutex);
        block->free = true;
        state->used -= size;
        --state->allocations;
        auto next = std::next(block);
        if (next != state->blocks.end() && next->free) {
            block->size += next->size;
            state->blocks.erase(next);
        }
        if (block != state->blocks.begin()) {
            auto prev = std::prev(block);
            if (prev->free) {
                prev->size += block->size;
                state->blocks.erase(block);
            }
        }
    }
};
}

namespace ii::webgpu {
MemoryBudget::MemoryBudget(std::uint64_t limit) : state_(std::make_shared<detail::BudgetState>()) {
    state_->stats.limit = limit;
}
Result<Reservation> MemoryBudget::reserve(std::uint64_t bytes) const {
    if (!bytes) return std::unexpected(Error{ErrorCode::InvalidArgument, "reservation must be nonzero"});
    auto lease = std::make_shared<detail::BudgetLease>();
    lease->state = state_;
    lease->bytes = bytes;
    std::lock_guard lock(state_->mutex);
    auto& s = state_->stats;
    if (bytes > s.limit - s.used) return std::unexpected(Error{ErrorCode::BudgetExceeded, "logical GPU memory budget exceeded"});
    s.used += bytes;
    ++s.allocations;
    s.peak = std::max(s.peak, s.used);
    lease->active = true;
    return Reservation(std::move(lease));
}
Status MemoryBudget::set_limit(std::uint64_t bytes) const {
    std::lock_guard lock(state_->mutex);
    if (bytes < state_->stats.used) return std::unexpected(Error{ErrorCode::BudgetExceeded, "new limit is below live reservations"});
    state_->stats.limit = bytes;
    return {};
}
MemoryStats MemoryBudget::stats() const { std::lock_guard lock(state_->mutex); return state_->stats; }
std::uint64_t Reservation::size() const noexcept { return lease_ ? lease_->bytes : 0; }
RangeAllocator::RangeAllocator(std::uint64_t capacity) : state_(std::make_shared<detail::RangeState>()) {
    state_->capacity = capacity;
    if (capacity) state_->blocks.push_back({0, capacity, true});
}
Result<RangeAllocation> RangeAllocator::allocate(std::uint64_t bytes, std::uint64_t alignment) const {
    if (!bytes || !std::has_single_bit(alignment))
        return std::unexpected(Error{ErrorCode::InvalidArgument, "size must be nonzero and alignment a power of two"});
    auto lease = std::make_shared<detail::RangeLease>();
    lease->state = state_;
    std::lock_guard lock(state_->mutex);
    for (auto i = state_->blocks.begin(); i != state_->blocks.end(); ++i) {
        if (!i->free) continue;
        const auto padding = (alignment - (i->offset & (alignment - 1))) & (alignment - 1);
        if (padding > i->size || bytes > i->size - padding) continue;
        // Prepare every allocating operation before changing the free list.
        std::list<detail::Block> pieces;
        if (padding) pieces.push_back({i->offset, padding, true});
        pieces.push_back({i->offset + padding, bytes, false});
        auto allocated = std::prev(pieces.end());
        if (i->size - padding - bytes) pieces.push_back({allocated->offset + bytes, i->size - padding - bytes, true});
        lease->offset = allocated->offset;
        lease->size = bytes;
        lease->block = allocated;
        state_->blocks.splice(i, pieces);
        state_->blocks.erase(i);
        state_->used += bytes;
        ++state_->allocations;
        lease->active = true;
        return RangeAllocation(std::move(lease));
    }
    return std::unexpected(Error{ErrorCode::Exhausted, "no aligned contiguous range available"});
}
RangeStats RangeAllocator::stats() const {
    std::lock_guard lock(state_->mutex);
    std::uint64_t largest = 0;
    for (const auto& block : state_->blocks) if (block.free) largest = std::max(largest, block.size);
    return {state_->capacity, state_->used, largest, state_->allocations};
}
std::uint64_t RangeAllocation::offset() const noexcept { return lease_ ? lease_->offset : 0; }
std::uint64_t RangeAllocation::size() const noexcept { return lease_ ? lease_->size : 0; }
}
