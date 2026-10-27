#include <iiWebGPU/Arena.h>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace ii::webgpu;
namespace {
template<class T> T take(Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void take(Status result) { if (!result) throw std::runtime_error(result.error().message); }
template<class T> T wait(Future<T> future) { return take(future.wait()); }
template<> void wait(Future<void> future) { take(future.wait()); }
}
int main(int argc, char** argv) {
    try {
        WGPURequestAdapterOptions selection=WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        selection.powerPreference=WGPUPowerPreference_HighPerformance;
        if (argc>1) {
            const std::string_view backend=argv[1];
            if (backend=="metal") selection.backendType=WGPUBackendType_Metal;
            else if (backend=="vulkan") selection.backendType=WGPUBackendType_Vulkan;
            else if (backend=="d3d12") selection.backendType=WGPUBackendType_D3D12;
            else throw std::runtime_error("backend must be metal, vulkan or d3d12");
        }
        auto context=take(Context::create());
        auto adapter=wait(context.request_adapter(selection));
        auto info=adapter.info();
        if (info.type!=WGPUAdapterType_DiscreteGPU && info.type!=WGPUAdapterType_IntegratedGPU)
            throw std::runtime_error("a hardware GPU is required; CPU fallback is disabled");
        DeviceOptions options;
        options.memory_budget=1024*1024;
        auto device=wait(adapter.request_device(options));
        std::cout<<"device="<<info.device<<" backend="<<static_cast<int>(info.backend)<<'\n';
        auto shader=wait(device.create_shader(R"(
@group(0) @binding(0) var<storage, read_write> values: array<u32>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3u) {
    if (id.x < arrayLength(&values)) { values[id.x] = values[id.x] * 3u + 7u; }
})"));
        WGPUComputePipelineDescriptor compute=WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        compute.compute.module=shader.native(); compute.compute.entryPoint=string_view("main");
        auto pipeline=wait(device.create_compute_pipeline(compute));
        WGPUBufferDescriptor storage_descriptor=WGPU_BUFFER_DESCRIPTOR_INIT;
        storage_descriptor.size=8192;
        storage_descriptor.usage=WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst;
        auto storage=wait(device.create_buffer(storage_descriptor));
        BufferArena arena(storage,device.limits().minStorageBufferOffsetAlignment);
        auto input=take(arena.allocate(4096));
        std::array<std::uint32_t,1024> values{};
        for (std::uint32_t i=0;i<values.size();++i) values[i]=i;
        take(device.write(input,std::as_bytes(std::span(values))));
        auto layout=BindGroupLayout::adopt(wgpuComputePipelineGetBindGroupLayout(pipeline.native(),0));
        WGPUBindGroupEntry entry=WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding=0; entry.buffer=input.native(); entry.offset=input.offset(); entry.size=input.size();
        WGPUBindGroupDescriptor bindings=WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindings.layout=layout.native(); bindings.entryCount=1; bindings.entries=&entry;
        auto group=wait(device.create_bind_group(bindings,std::span(&input,1)));
        WGPUBufferDescriptor readback_descriptor=WGPU_BUFFER_DESCRIPTOR_INIT;
        readback_descriptor.size=4096; readback_descriptor.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_CopyDst;
        auto readback=wait(device.create_buffer(readback_descriptor));
        auto commands=take(device.commands("explicit compute and readback"));
        take(commands.dispatch(pipeline,std::span(&group,1),16));
        take(commands.copy(input,readback.whole(),4096));
        wait(commands.submit());
        auto mapping=wait(device.map(readback.whole(),WGPUMapMode_Read));
        std::array<std::uint32_t,1024> output{};
        std::memcpy(output.data(),mapping.bytes().data(),sizeof(output));
        for (std::size_t i=0;i<values.size();++i)
            if(output[i]!=values[i]*3u+7u) throw std::runtime_error("GPU result mismatch");
        std::cout<<"EXECUTED WebGPU: 1024 verified u32 values, no CPU fallback; logical bytes="<<device.memory().used<<'\n';
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
