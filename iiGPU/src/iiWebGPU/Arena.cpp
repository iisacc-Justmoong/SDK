#include "Arena.h"
#include <algorithm>
#include <bit>

namespace ii::webgpu {
BufferArena::BufferArena(Buffer buffer, std::uint64_t minimum_alignment)
    : buffer_(std::move(buffer)), ranges_(buffer_.size()), minimum_alignment_(minimum_alignment) {}
Result<BufferSlice> BufferArena::allocate(std::uint64_t bytes, std::uint64_t alignment) const {
    if (!std::has_single_bit(alignment) || !std::has_single_bit(minimum_alignment_) || bytes % 4)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "arena sizes must be multiples of four and alignments powers of two"});
    auto allocation = ranges_.allocate(bytes, std::max({alignment, minimum_alignment_, std::uint64_t{4}}));
    if (!allocation) return std::unexpected(allocation.error());
    auto slice = buffer_.slice(allocation->offset(), bytes);
    if (!slice) return slice;
    slice->allocation_ = std::move(*allocation);
    return slice;
}
}
