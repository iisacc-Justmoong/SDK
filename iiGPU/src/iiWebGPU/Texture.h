#pragma once
#include "Native.h"
#include "Result.h"
#include <cstdint>

namespace ii::webgpu {
// Format/mip/layer/sample footprint used for logical allocation accounting.
// Driver padding, residency and heap layout remain implementation-owned.
[[nodiscard]] Result<std::uint64_t> texture_bytes(const WGPUTextureDescriptor&);
}
