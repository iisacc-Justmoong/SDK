#include <iiWebGPU/Arena.h>
#include <array>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>

using namespace ii::webgpu;
namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> T get(Result<T> result) {
    if (!result) throw std::runtime_error(result.error().message);
    return std::move(*result);
}
void get(Status result) { if (!result) throw std::runtime_error(result.error().message); }
template<class T> T await(Future<T> future) { return get(future.wait()); }
template<> void await(Future<void> future) { get(future.wait()); }
struct Host final {
    Context context = get(Context::create());
    Adapter adapter = await(context.request_adapter());
    Device device = await(adapter.request_device(DeviceOptions{.memory_budget=4*1024*1024}));
    Host() {
        const auto info = adapter.info();
        require(info.type == WGPUAdapterType_DiscreteGPU || info.type == WGPUAdapterType_IntegratedGPU,
            "hardware adapter required; software/CPU adapter is not acceleration evidence");
        require(get(adapter.limits()).maxBufferSize > 0, "adapter limits");
        std::cout << "HARDWARE device=" << info.device << " backend=" << static_cast<int>(info.backend)
                  << " adapter=" << static_cast<int>(info.type) << '\n';
    }
    ~Host() {
        device.poll();
        const auto diagnostics = device.take_diagnostics();
        for (const auto& diagnostic : diagnostics) std::cerr << "diagnostic: " << diagnostic.message << '\n';
    }
};
Buffer buffer(const Device& device, std::uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor descriptor = WGPU_BUFFER_DESCRIPTOR_INIT;
    descriptor.size = size; descriptor.usage = usage;
    return await(device.create_buffer(descriptor));
}
constexpr std::string_view kernel = R"(
@group(0) @binding(0) var<storage, read_write> values: array<u32>;
@compute @workgroup_size(64)
fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    if (id.x < arrayLength(&values)) { values[id.x] = values[id.x] * 3u + 7u; }
})";
ComputePipeline pipeline(const Device& device, const ShaderModule& shader) {
    WGPUBindGroupLayoutEntry entry=WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding=0; entry.visibility=WGPUShaderStage_Compute;
    entry.buffer.type=WGPUBufferBindingType_Storage; entry.buffer.minBindingSize=4;
    WGPUBindGroupLayoutDescriptor group_descriptor=WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    group_descriptor.entryCount=1; group_descriptor.entries=&entry;
    auto group_layout=await(device.create_bind_group_layout(group_descriptor));
    WGPUBindGroupLayout raw=group_layout.native();
    WGPUPipelineLayoutDescriptor layout_descriptor=WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layout_descriptor.bindGroupLayoutCount=1; layout_descriptor.bindGroupLayouts=&raw;
    auto layout=await(device.create_pipeline_layout(layout_descriptor));
    WGPUComputePipelineDescriptor descriptor = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    descriptor.layout=layout.native();
    descriptor.compute.module = shader.native(); descriptor.compute.entryPoint = string_view("main");
    return await(device.create_compute_pipeline(descriptor));
}
BindGroup binding(const Device& device, const ComputePipeline& pipeline, const BufferSlice& slice) {
    auto layout = BindGroupLayout::adopt(wgpuComputePipelineGetBindGroupLayout(pipeline.native(),0));
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0; entry.buffer = slice.native(); entry.offset = slice.offset(); entry.size = slice.size();
    WGPUBindGroupDescriptor descriptor = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    descriptor.layout = layout.native(); descriptor.entryCount = 1; descriptor.entries = &entry;
    return await(device.create_bind_group(descriptor, std::span(&slice,1)));
}
void footprint() {
    WGPUTextureDescriptor d = WGPU_TEXTURE_DESCRIPTOR_INIT;
    d.dimension = WGPUTextureDimension_2D; d.size = {8,4,3};
    d.mipLevelCount = 4; d.format = WGPUTextureFormat_RGBA8Unorm;
    require(get(texture_bytes(d)) == (32+8+2+1)*3*4, "2D layers were incorrectly mip-reduced");
    d.dimension = WGPUTextureDimension_3D; d.size = {8,4,4};
    require(get(texture_bytes(d)) == (128+16+2+1)*4, "3D mip depth");
    d.dimension = WGPUTextureDimension_2D; d.size = {7,5,1}; d.mipLevelCount=1; d.format=WGPUTextureFormat_BC1RGBAUnorm;
    require(get(texture_bytes(d))==32, "compressed block rounding");
    d.format=WGPUTextureFormat_ASTC5x4Unorm;
    require(get(texture_bytes(d))==64, "ASTC block footprint");
    d.size={8,4,1}; d.format=WGPUTextureFormat_RGBA8Unorm; d.sampleCount=4;
    require(get(texture_bytes(d))==512, "multisample footprint");
    d.size={0xffffffffu,0xffffffffu,0xffffffffu}; d.format=WGPUTextureFormat_RGBA32Float;
    require(!texture_bytes(d), "footprint arithmetic overflow");
    d.format=WGPUTextureFormat_Undefined;
    require(!texture_bytes(d), "unknown format accepted");
    d.format=WGPUTextureFormat_RGBA8Unorm; d.size={4,4,1}; d.sampleCount=1; d.mipLevelCount=4;
    require(!texture_bytes(d), "invalid mip count");
}
void capabilities() {
    Host host;
    auto adapter=await(host.context.request_adapter());
    auto available=adapter.features();
    WGPULimits requested=WGPU_LIMITS_INIT;
    requested.maxComputeWorkgroupsPerDimension=128;
    DeviceOptions options;
    options.descriptor.requiredLimits=&requested;
    const WGPUFeatureName feature=WGPUFeatureName_TimestampQuery;
    const bool timestamp=std::find(available.begin(),available.end(),feature)!=available.end();
    if (timestamp) { options.descriptor.requiredFeatureCount=1; options.descriptor.requiredFeatures=&feature; }
    auto device=await(adapter.request_device(options));
    require(device.limits().maxComputeWorkgroupsPerDimension>=128,"required limits not negotiated");
    auto sampler=await(device.create_sampler());
    require(static_cast<bool>(sampler),"sampler creation");
    if(timestamp) {
        require(wgpuDeviceHasFeature(device.native(),feature),"requested feature absent from device");
        WGPUQuerySetDescriptor query=WGPU_QUERY_SET_DESCRIPTOR_INIT;
        query.type=WGPUQueryType_Timestamp; query.count=2;
        auto set=await(device.create_query_set(query));
        require(wgpuQuerySetGetCount(set.native())==2,"query set descriptor ignored");
        std::cout<<"CAPABILITY timestamp-query negotiated and query set allocated\n";
    } else std::cout<<"OPTIONAL timestamp-query not advertised on this hardware\n";
    requested.maxComputeWorkgroupSizeX=std::numeric_limits<std::uint32_t>::max()-1;
    auto unsupported=await(host.context.request_adapter());
    require(!unsupported.request_device(options).wait(),"unsupported required limit accepted");
}
void compute() {
    Host host;
    auto& device = host.device;
    auto shader = await(device.create_shader(kernel));
    auto program = pipeline(device,shader);
    auto storage = buffer(device,8192,WGPUBufferUsage_Storage|WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    BufferArena arena(storage,device.limits().minStorageBufferOffsetAlignment);
    auto padding = get(arena.allocate(256));
    auto slice = get(arena.allocate(4096));
    require(slice.offset()>=256, "suballocated offset lost");
    std::array<std::uint32_t,1024> input{};
    for (std::uint32_t i=0; i<input.size(); ++i) input[i]=i;
    input[1]=16777217u; input[2]=0xffffffffu;
    get(device.write(slice,std::as_bytes(std::span(input))));
    auto group = binding(device,program,slice);
    auto readback = buffer(device,4096,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    auto commands = get(device.commands("exact u32 affine kernel"));
    get(commands.dispatch(program,std::span(&group,1),16));
    get(commands.copy(slice,readback.whole(),4096));
    await(commands.submit());
    {
        auto mapping = await(device.map(readback.whole(),WGPUMapMode_Read));
        require(!mapping.writable_bytes(), "read mapping exposed mutable memory");
        std::array<std::uint32_t,1024> result{};
        std::memcpy(result.data(),mapping.bytes().data(),4096);
        for (std::size_t i=0; i<input.size(); ++i) require(result[i]==input[i]*3u+7u,"GPU kernel result mismatch");
    }
    require(wgpuBufferGetMapState(readback.native())==WGPUBufferMapState_Unmapped,"last mapping owner did not unmap");
    await(device.on_submitted_work_done());
    require(device.take_diagnostics().empty(),"uncaptured compute errors");
    std::cout << "EXECUTED compute: 1024 exact u32 values, suballocated offset=" << slice.offset() << ", no CPU fallback\n";
}
void render() {
    Host host;
    auto& device = host.device;
    constexpr std::string_view source = R"(
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4f {
    let p = array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3));
    return vec4f(p[i],0,1);
}
@fragment fn fs() -> @location(0) vec4f { return vec4f(0.25,0.5,0.75,1); }
)";
    auto shader = await(device.create_shader(source));
    WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
    color.format=WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module=shader.native(); fragment.entryPoint=string_view("fs"); fragment.targetCount=1; fragment.targets=&color;
    WGPURenderPipelineDescriptor descriptor = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    descriptor.vertex.module=shader.native(); descriptor.vertex.entryPoint=string_view("vs"); descriptor.fragment=&fragment;
    auto pipeline = await(device.create_render_pipeline(descriptor));
    WGPUTextureDescriptor texture_descriptor = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texture_descriptor.dimension=WGPUTextureDimension_2D; texture_descriptor.size={16,16,1};
    texture_descriptor.format=WGPUTextureFormat_RGBA8Unorm;
    texture_descriptor.usage=WGPUTextureUsage_RenderAttachment|WGPUTextureUsage_CopySrc;
    auto texture = await(device.create_texture(texture_descriptor));
    require(texture.logical_bytes()==1024,"texture budget");
    auto view=await(texture.view());
    auto readback=buffer(device,256*16,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    auto commands=get(device.commands("offscreen triangle"));
    get(commands.retain(texture)); get(commands.retain(readback.whole()));
    WGPURenderPassColorAttachment attachment=WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view=view.native(); attachment.loadOp=WGPULoadOp_Clear; attachment.storeOp=WGPUStoreOp_Store;
    attachment.clearValue={0,0,0,0};
    WGPURenderPassDescriptor pass_descriptor=WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    pass_descriptor.colorAttachmentCount=1; pass_descriptor.colorAttachments=&attachment;
    auto pass=Handle<WGPURenderPassEncoder>::adopt(wgpuCommandEncoderBeginRenderPass(commands.native(),&pass_descriptor));
    wgpuRenderPassEncoderSetPipeline(pass.native(),pipeline.native());
    wgpuRenderPassEncoderDraw(pass.native(),3,1,0,0);
    wgpuRenderPassEncoderEnd(pass.native());
    WGPUTexelCopyTextureInfo from=WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    from.texture=texture.native();
    WGPUTexelCopyBufferInfo to=WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    to.buffer=readback.native(); to.layout.bytesPerRow=256; to.layout.rowsPerImage=16;
    WGPUExtent3D size={16,16,1};
    wgpuCommandEncoderCopyTextureToBuffer(commands.native(),&from,&to,&size);
    await(commands.submit());
    {
        auto mapping=await(device.map(readback.whole(),WGPUMapMode_Read));
        for (std::size_t y=0;y<16;++y) for (std::size_t x=0;x<16;++x) {
            const auto* pixel=reinterpret_cast<const unsigned char*>(mapping.bytes().data()+y*256+x*4);
            require(pixel[0]>=63 && pixel[0]<=64 && pixel[1]>=127 && pixel[1]<=128 && pixel[2]>=191 && pixel[2]<=192 && pixel[3]==255,
                "rendered pixel mismatch");
        }
    }
    require(device.take_diagnostics().empty(),"uncaptured render errors");
    std::cout << "EXECUTED render: fullscreen triangle, 256 validated RGBA pixels\n";
}
void validation() {
    Host host;
    auto& device=host.device;
    require(!device.create_shader("invalid WGSL").wait(),"shader errors were hidden");
    const auto baseline=device.memory().used;
    WGPUBufferDescriptor invalid=WGPU_BUFFER_DESCRIPTOR_INIT;
    invalid.size=256; invalid.usage=WGPUBufferUsage_MapRead|WGPUBufferUsage_Storage;
    auto rejected=device.create_buffer(invalid).wait();
    require(!rejected && rejected.error().code==ErrorCode::Validation,"backend validation was not returned");
    require(device.memory().used==baseline,"failed allocation leaked its budget reservation");
    auto storage=buffer(device,1024,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    get(device.set_memory_budget(1024));
    require(!device.create_buffer(invalid).wait(),"budget rejection");
    require(!device.set_memory_budget(1023),"shrunk below live memory");
    require(!storage.slice(1020,8),"out-of-bounds slice");
    std::array<std::byte,3> odd{};
    require(!device.write(storage.whole(),odd),"unaligned upload");
    require(!device.map(storage.whole(),WGPUMapMode_Read).wait(),"mapped buffer without MapRead usage");
    auto commands=get(device.commands());
    auto a=get(storage.slice(0,256)), b=get(storage.slice(128,256));
    require(!commands.copy(a,b,256),"overlapping copy");
    require(!commands.copy(a,get(storage.slice(512,256)),256),"same-buffer disjoint copy");
    WGPUBindGroupDescriptor empty_entries=WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    empty_entries.entryCount=1;
    require(!device.create_bind_group(empty_entries).wait(),"null bind group entries");
    await(commands.submit());
    require(!commands.submit().wait(),"encoder submitted twice");
    get(device.set_memory_budget(4*1024*1024));
    auto writable=buffer(device,16,WGPUBufferUsage_MapWrite|WGPUBufferUsage_CopySrc);
    {
        auto future=device.map(writable.whole(),WGPUMapMode_Write);
        require(!future.result() && future.result().error().code==ErrorCode::NotReady,"pending map result");
        require(!future.wait(std::chrono::nanoseconds(0)) && future.wait(std::chrono::nanoseconds(0)).error().code==ErrorCode::Timeout,
            "zero wait must time out without cancelling the pending map");
        auto mapping=get(future.wait());
        auto copy=mapping;
        auto bytes=get(mapping.writable_bytes());
        std::fill(bytes.begin(),bytes.end(),std::byte{0x5a});
        require(!device.map(writable.whole(),WGPUMapMode_Write).wait(),"double mapping");
        auto copy_commands=get(device.commands());
        require(!copy_commands.retain(writable.whole()),"mapped resource accepted in GPU command");
    }
    require(wgpuBufferGetMapState(writable.native())==WGPUBufferMapState_Unmapped,"write mapping did not unmap");
    auto readback=buffer(device,16,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
    auto upload=get(device.commands());
    get(upload.copy(writable.whole(),readback.whole(),16));
    await(upload.submit());
    {
        auto mapping=await(device.map(readback.whole(),WGPUMapMode_Read));
        for (auto byte : mapping.bytes()) require(byte==std::byte{0x5a},"mapped upload did not reach the GPU transfer");
    }
    device.destroy(); device.poll();
    require(device.lost() && !device.commands(),"destroy did not invalidate the device");
    require(!device.create_shader(kernel).wait(),"lost device accepted shader");
    std::cout << "validation, rollback, map permissions and device loss passed\n";
}
void lifetime() {
    Host host;
    auto& device=host.device;
    auto base=buffer(device,1024,WGPUBufferUsage_CopySrc|WGPUBufferUsage_CopyDst);
    BufferArena arena(base);
    Future<void> submitted;
    {
        auto slice=get(arena.allocate(1024));
        auto commands=get(device.commands());
        get(commands.clear(slice));
        slice={};
        require(!arena.allocate(4),"encoded range reused before submission");
        submitted=commands.submit();
        require(!arena.allocate(4),"submitted range reused before completion callback");
    }
    await(submitted);
    require(arena.stats().used==0 && get(arena.allocate(1024)).size()==1024,"range not reclaimed on completion");
    {
        auto transient=buffer(device,512,WGPUBufferUsage_CopyDst);
        auto commands=get(device.commands());
        get(commands.clear(transient.whole()));
        transient={};
        require(device.memory().used==1536,"encoded buffer budget released early");
        await(commands.submit());
        require(device.memory().used==1024,"completed resource reservation not reclaimed");
    }
    TextureView retained_view;
    {
        WGPUTextureDescriptor descriptor=WGPU_TEXTURE_DESCRIPTOR_INIT;
        descriptor.dimension=WGPUTextureDimension_2D; descriptor.size={4,4,1};
        descriptor.format=WGPUTextureFormat_RGBA8Unorm; descriptor.usage=WGPUTextureUsage_TextureBinding;
        auto texture=await(device.create_texture(descriptor));
        WGPUTextureViewDescriptor invalid=WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        invalid.dimension=WGPUTextureViewDimension_3D;
        require(!texture.view(&invalid).wait(),"invalid texture view did not return validation failure");
        retained_view=await(texture.view());
    }
    require(device.memory().used==1088,"texture view did not retain the texture reservation");
    retained_view={};
    require(device.memory().used==1024,"texture view reservation was not reclaimed");
    Future<Buffer> owned_allocation;
    {
        auto adapter=await(host.context.request_adapter());
        auto owner=await(adapter.request_device());
        WGPUBufferDescriptor descriptor=WGPU_BUFFER_DESCRIPTOR_INIT;
        descriptor.size=64; descriptor.usage=WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead;
        owned_allocation=owner.create_buffer(descriptor);
    }
    auto owned_buffer=await(owned_allocation);
    require(owned_buffer.size()==64,"pending allocation did not retain its device owner");
    Future<Mapping> owned_mapping;
    {
        auto adapter=await(host.context.request_adapter());
        auto owner=await(adapter.request_device());
        auto source=buffer(owner,64,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
        owned_mapping=owner.map(source.whole(),WGPUMapMode_Read);
    }
    auto mapped=await(owned_mapping);
    require(mapped.bytes().size()==64,"pending mapping did not retain its device owner");
    for (int i=0;i<8;++i) {
        auto context=get(Context::create());
        auto adapter=await(context.request_adapter());
        auto dev=await(adapter.request_device());
        { auto ignored=dev.create_shader(kernel); }
        WGPUBufferDescriptor pending=WGPU_BUFFER_DESCRIPTOR_INIT;
        pending.size=4096; pending.usage=WGPUBufferUsage_CopyDst;
        { auto ignored=dev.create_buffer(pending); }
        auto readback=buffer(dev,64,WGPUBufferUsage_CopyDst|WGPUBufferUsage_MapRead);
        { auto ignored=dev.map(readback.whole(),WGPUMapMode_Read); }
        auto commands=get(dev.commands());
        auto other=buffer(dev,64,WGPUBufferUsage_CopyDst);
        get(commands.clear(other.whole()));
        { auto ignored=commands.submit(); }
        // Deliberately leave pending callbacks unobserved during teardown.
    }
    std::cout << "deferred retirement and abandoned-operation teardown passed\n";
}
}
int main(int argc,char** argv) {
    try {
        const std::string_view test=argc>1 ? argv[1] : "Compute";
        if(test=="Footprint") footprint(); else if(test=="Capabilities") capabilities(); else if(test=="Compute") compute(); else if(test=="Render") render();
        else if(test=="Validation") validation(); else if(test=="Lifetime") lifetime(); else throw std::runtime_error("unknown test");
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
