#include "Backend.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <TargetConditionals.h>
#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <cstdlib>

namespace ii::gpu::detail {
namespace {
std::string message(NSError* error,const char* fallback) {
    return error?std::string(error.localizedDescription.UTF8String):std::string(fallback);
}
NSArray<id<MTLDevice>>* devices() {
#if TARGET_OS_OSX
    return MTLCopyAllDevices();
#else
    auto device=MTLCreateSystemDefaultDevice(); return device?@[device]:@[];
#endif
}
Adapter describe(id<MTLDevice> device,std::size_t index) {
    DeviceKind kind=DeviceKind::unknown;
#if TARGET_OS_OSX
    kind=device.hasUnifiedMemory?DeviceKind::integrated:DeviceKind::discrete;
#else
    kind=DeviceKind::integrated;
#endif
    return {Backend::metal,index,device.name.UTF8String,kind,true};
}
struct MetalBuffer final : NativeBuffer {
    id<MTLBuffer> buffer;
    explicit MetalBuffer(id<MTLBuffer> value):buffer(value) {}
    void upload(std::span<const std::byte> bytes,std::uint64_t offset) override {
        if(!bytes.empty()) std::memcpy(static_cast<std::byte*>(buffer.contents)+offset,bytes.data(),bytes.size());
    }
    void read(std::span<std::byte> bytes,std::uint64_t offset) override {
        if(!bytes.empty()) std::memcpy(bytes.data(),static_cast<const std::byte*>(buffer.contents)+offset,bytes.size());
    }
    std::uint64_t handle() const override { return reinterpret_cast<std::uint64_t>((__bridge void*)buffer); }
    std::uint64_t allocation_bytes() const override { return buffer.allocatedSize; }
};
struct MetalPipeline final : NativePipeline {
    id<MTLComputePipelineState> pipeline;
    explicit MetalPipeline(id<MTLComputePipelineState> value):pipeline(value) {}
};
struct MetalSignal final {
    std::mutex mutex;
    std::condition_variable condition;
    bool done=false;
};
struct MetalSubmission final : NativeSubmission {
    id<MTLCommandBuffer> command;
    std::shared_ptr<MetalSignal> signal=std::make_shared<MetalSignal>();
    explicit MetalSubmission(id<MTLCommandBuffer> value):command(value) {
        auto completion=signal;
        [command addCompletedHandler:^(id<MTLCommandBuffer>) {
            { std::lock_guard lock(completion->mutex); completion->done=true; }
            completion->condition.notify_all();
        }];
    }
    bool wait(std::chrono::nanoseconds timeout) override {
        std::unique_lock lock(signal->mutex);
        if(timeout==std::chrono::nanoseconds::max()) signal->condition.wait(lock,[&]{return signal->done;});
        else if(!signal->condition.wait_for(lock,timeout,[&]{return signal->done;})) return false;
        require(command.status==MTLCommandBufferStatusCompleted,ErrorCode::backend_failure,
            message(command.error,"Metal command execution failed").c_str());
        return true;
    }
};
class MetalDevice final : public NativeDevice {
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id<MTLComputePipelineState> fill_;
public:
    explicit MetalDevice(const Options& options) {
        @autoreleasepool {
            auto all=devices();
            require(options.adapter_index<all.count,ErrorCode::unavailable,"Metal adapter index is unavailable");
            device_=all[options.adapter_index]; adapter=describe(device_,options.adapter_index);
            queue_=[device_ newCommandQueue];
            require(queue_!=nil,ErrorCode::backend_failure,"Metal queue creation failed");
            limits.max_buffer_bytes=device_.maxBufferLength;
            limits.max_bindings=16; limits.max_constants_bytes=128;
            // Grid dimensions are uint32 in kernels; the abstraction caps group
            // counts to avoid overflow when multiplied by the threadgroup size.
            limits.max_groups={65535,65535,65535};
            const auto group=device_.maxThreadsPerThreadgroup;
            limits.max_group_size={static_cast<std::uint32_t>(group.width),static_cast<std::uint32_t>(group.height),static_cast<std::uint32_t>(group.depth)};
            limits.max_threads_per_group=static_cast<std::uint32_t>(group.width);
            limits.binding_offset_alignment=256;
        }
    }
    std::unique_ptr<NativeBuffer> buffer(BufferDesc desc) override {
        @autoreleasepool {
            auto value=[device_ newBufferWithLength:desc.bytes options:desc.memory==Memory::shared?MTLResourceStorageModeShared:MTLResourceStorageModePrivate];
            require(value!=nil,ErrorCode::out_of_memory,"Metal buffer allocation failed");
            return std::make_unique<MetalBuffer>(value);
        }
    }
    std::unique_ptr<NativePipeline> pipeline(const ComputeDesc& desc) override {
        @autoreleasepool {
            NSError* error=nil;
            id<MTLLibrary> library=nil;
            if(!desc.metal_library.empty()) {
                void* copy=std::malloc(desc.metal_library.size());
                require(copy!=nullptr,ErrorCode::out_of_memory,"Metal library allocation failed");
                std::memcpy(copy,desc.metal_library.data(),desc.metal_library.size());
                auto data=dispatch_data_create(copy,desc.metal_library.size(),nullptr,DISPATCH_DATA_DESTRUCTOR_FREE);
                library=[device_ newLibraryWithData:data error:&error];
            } else {
                auto source=[[NSString alloc] initWithBytes:desc.metal_source.data() length:desc.metal_source.size() encoding:NSUTF8StringEncoding];
                require(source!=nil,ErrorCode::invalid_argument,"Metal source is not UTF-8");
                library=[device_ newLibraryWithSource:source options:nil error:&error];
            }
            if(!library) fail(ErrorCode::backend_failure,message(error,"Metal library creation failed"));
            auto function=[library newFunctionWithName:[NSString stringWithUTF8String:desc.metal_entry.c_str()]];
            require(function!=nil,ErrorCode::invalid_argument,"Metal entry point was not found");
            auto state=[device_ newComputePipelineStateWithFunction:function error:&error];
            if(!state) fail(ErrorCode::backend_failure,message(error,"Metal compute pipeline creation failed"));
            const auto threads=std::uint64_t(desc.group_size.x)*desc.group_size.y*desc.group_size.z;
            require(threads<=state.maxTotalThreadsPerThreadgroup,ErrorCode::limit_exceeded,"threadgroup exceeds Metal kernel limits");
            return std::make_unique<MetalPipeline>(state);
        }
    }
    std::unique_ptr<NativeSubmission> submit(const std::vector<Command>& commands) override {
        @autoreleasepool {
            auto command=[queue_ commandBuffer];
            require(command!=nil,ErrorCode::backend_failure,"Metal command buffer allocation failed");
            for(const auto& c:commands) {
                if(const auto* copy=std::get_if<Copy>(&c)) {
                    auto encoder=[command blitCommandEncoder];
                    require(encoder!=nil,ErrorCode::backend_failure,"Metal transfer encoder allocation failed");
                    [encoder copyFromBuffer:static_cast<MetalBuffer&>(*copy->source->native).buffer sourceOffset:copy->source_offset
                        toBuffer:static_cast<MetalBuffer&>(*copy->destination->native).buffer destinationOffset:copy->destination_offset size:copy->bytes];
                    [encoder endEncoding];
                } else if(const auto* fill=std::get_if<Fill>(&c)) {
                    if(!fill_) {
                        ComputeDesc desc;
                        desc.metal_source=R"(
                            #include <metal_stdlib>
                            using namespace metal;
                            kernel void main0(device uint* buffer [[buffer(0)]],constant uint2& p [[buffer(1)]],uint i [[thread_position_in_grid]]) {
                                if(i<p.y) buffer[i]=p.x;
                            })";
                        auto state=pipeline(desc);
                        fill_=static_cast<MetalPipeline&>(*state).pipeline;
                    }
                    require(fill->bytes/4<=UINT32_MAX,ErrorCode::limit_exceeded,"Metal fill exceeds uint32 addressing");
                    auto encoder=[command computeCommandEncoder];
                    require(encoder!=nil,ErrorCode::backend_failure,"Metal fill encoder allocation failed");
                    [encoder setComputePipelineState:fill_];
                    [encoder setBuffer:static_cast<MetalBuffer&>(*fill->buffer->native).buffer offset:fill->offset atIndex:0];
                    const std::uint32_t params[]{fill->pattern,static_cast<std::uint32_t>(fill->bytes/4)};
                    [encoder setBytes:params length:sizeof(params) atIndex:1];
                    const auto width=std::min<NSUInteger>(64,fill_.maxTotalThreadsPerThreadgroup);
                    [encoder dispatchThreads:MTLSizeMake(fill->bytes/4,1,1) threadsPerThreadgroup:MTLSizeMake(width,1,1)];
                    [encoder endEncoding];
                } else if(const auto* dispatch=std::get_if<Dispatch>(&c)) {
                    auto encoder=[command computeCommandEncoder];
                    require(encoder!=nil,ErrorCode::backend_failure,"Metal compute encoder allocation failed");
                    [encoder setComputePipelineState:static_cast<MetalPipeline&>(*dispatch->pipeline->native).pipeline];
                    for(std::size_t i=0;i<dispatch->bindings.size();++i) {
                        const auto& binding=dispatch->bindings[i];
                        [encoder setBuffer:static_cast<MetalBuffer&>(*binding.buffer->native).buffer offset:binding.offset atIndex:i];
                    }
                    if(!dispatch->constants.empty()) [encoder setBytes:dispatch->constants.data() length:dispatch->constants.size() atIndex:dispatch->bindings.size()];
                    const auto size=dispatch->pipeline->desc.group_size;
                    [encoder dispatchThreadgroups:MTLSizeMake(dispatch->groups.x,dispatch->groups.y,dispatch->groups.z)
                        threadsPerThreadgroup:MTLSizeMake(size.x,size.y,size.z)];
                    [encoder endEncoding];
                }
                // All encoders end here. Metal's tracked resources serialize
                // dependencies across these nonconcurrent encoder boundaries.
            }
            auto result=std::make_unique<MetalSubmission>(command);
            [command commit];
            return result;
        }
    }
    NativeContext context() const override {
        NativeContext c{}; c.backend=Backend::metal;
        c.device=reinterpret_cast<std::uint64_t>((__bridge void*)device_);
        c.queue=reinterpret_cast<std::uint64_t>((__bridge void*)queue_); return c;
    }
};
}
std::vector<Adapter> metal_adapters() {
    @autoreleasepool {
        std::vector<Adapter> result;
        for(id<MTLDevice> device in devices()) result.push_back(describe(device,result.size()));
        return result;
    }
}
std::unique_ptr<NativeDevice> metal_device(const Options& options) { return std::make_unique<MetalDevice>(options); }
} // namespace ii::gpu::detail
