#pragma once
#include "WebGPU.h"

namespace ii::webgpu {
// One explicit WebGPU buffer, reusable aligned ranges, no hidden pool growth.
class BufferArena final {
public:
    BufferArena(Buffer buffer, std::uint64_t minimum_alignment = 4);
    [[nodiscard]] Result<BufferSlice> allocate(std::uint64_t bytes, std::uint64_t alignment = 4) const;
    [[nodiscard]] RangeStats stats() const { return ranges_.stats(); }
    [[nodiscard]] const Buffer& buffer() const noexcept { return buffer_; }
private:
    Buffer buffer_;
    RangeAllocator ranges_;
    std::uint64_t minimum_alignment_;
};
}
