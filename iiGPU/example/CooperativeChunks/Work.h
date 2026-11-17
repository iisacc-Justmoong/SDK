#pragma once
#include <iiGPU/GPU.h>
#include <iiSDKCore/Runtime/ChunkQueue.h>
#include <array>
#include <span>
#include <vector>

namespace example {
inline ii::core::Result<void> gpu_result(ii::gpu::Result<void> result) {
    if(!result)return ii::core::fail(ii::core::ErrorCode::failed,result.error().message);
    return {};
}
inline ii::runtime::ChunkWork transform_work(ii::gpu::Device device,ii::gpu::ComputePipeline pipeline,
    const std::vector<std::uint32_t>& input,std::vector<std::uint32_t>& output,std::uint32_t chunk_items) {
    using namespace ii;
    runtime::ChunkWork work;work.items=input.size();work.chunk_items=chunk_items;
    work.cpu=[&input,&output](runtime::ChunkRange range,std::stop_token stop)->core::Result<void> {
        for(std::uint64_t i=range.offset;i<range.offset+range.count;++i) {
            if(stop.stop_requested())return core::fail(core::ErrorCode::cancelled,"CPU chunk stopped");
            output[i]=input[i]*7+13;
        }
        return {};
    };
    work.device=[device,pipeline,&input,&output](std::size_t,runtime::ChunkDispatch dispatch,std::stop_token stop)
        ->core::Result<runtime::ChunkSubmission> {
        if(stop.stop_requested())return core::fail(core::ErrorCode::cancelled,"Device submission stopped");
        const auto bytes=std::uint64_t(dispatch.range.count)*sizeof(std::uint32_t);
        auto source=device.buffer({bytes});if(!source)return core::fail(core::ErrorCode::failed,source.error().message);
        auto destination=device.buffer({bytes});if(!destination)return core::fail(core::ErrorCode::failed,destination.error().message);
        auto input_part=std::span(input).subspan(static_cast<std::size_t>(dispatch.range.offset),dispatch.range.count);
        if(auto result=source->upload(std::as_bytes(input_part));!result)return core::fail(core::ErrorCode::failed,result.error().message);
        const std::array<gpu::Binding,2> bindings{{{*source},{*destination}}};
        const std::array<std::uint32_t,3> constants{dispatch.range.count,7,13};
        auto commands=device.commands();
        if(auto result=commands.dispatch(pipeline,bindings,{dispatch.groups,1,1},std::as_bytes(std::span(constants)));!result)
            return core::fail(core::ErrorCode::failed,result.error().message);
        if(auto result=commands.finish();!result)return core::fail(core::ErrorCode::failed,result.error().message);
        auto submission=device.submit(std::move(commands),stop);
        if(!submission)return core::fail(core::ErrorCode::failed,submission.error().message);
        runtime::ChunkSubmission pending;
        pending.wait=[submission=*submission]{return gpu_result(submission.wait());};
        pending.collect=[buffer=*destination,&output,range=dispatch.range] {
            auto values=std::span(output).subspan(static_cast<std::size_t>(range.offset),range.count);
            return gpu_result(buffer.read(std::as_writable_bytes(values)));
        };
        return pending;
    };
    return work;
}
} // namespace example
