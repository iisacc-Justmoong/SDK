#include <iiGPU/GPU.h>
#include <iiWebGPU/Arena.h>
#include <iiWebGPU/Memory.h>
#include <iiWebGPU/Native.h>
#include <iiWebGPU/Result.h>
#include <iiWebGPU/Texture.h>
#include <iiWebGPU/WebGPU.h>
#include <cstdint>

int main() {
    // Both interfaces must coexist in one package without depending on Core.
    ii::gpu::Extent shape{2, 3, 4};
    ii::webgpu::MemoryBudget budget{64};
    auto lease = budget.reserve(32);
    if (shape.x * shape.y * shape.z != 24 || !lease || budget.stats().used != 32)
        return 1;
    if (budget.reserve(33)) return 2;
    return 0;
}
