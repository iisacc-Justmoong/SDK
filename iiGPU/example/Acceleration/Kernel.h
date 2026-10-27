#pragma once
#include <iiGPU/GPU.h>
#include "Shader.h"
#include <stdexcept>

namespace example {
template<class T> T take(ii::gpu::Result<T> value) {
    if(!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}
inline void take(ii::gpu::Result<void> value) {
    if(!value) throw std::runtime_error(value.error().message);
}
inline ii::gpu::ComputeDesc transform() {
    ii::gpu::ComputeDesc desc;
    desc.metal_source=R"(
        #include <metal_stdlib>
        using namespace metal;
        struct Parameters { uint count,scale,bias; };
        kernel void main0(device const uint* values [[buffer(0)]],
            device uint* result [[buffer(1)]],constant Parameters& p [[buffer(2)]],
            uint i [[thread_position_in_grid]]) {
            if(i<p.count) result[i]=values[i]*p.scale+p.bias;
        })";
    desc.spirv.assign(std::begin(transform_spirv),std::end(transform_spirv));
    desc.bindings={ii::gpu::Access::read,ii::gpu::Access::write};
    desc.constants_bytes=12;
    return desc;
}
} // namespace example
