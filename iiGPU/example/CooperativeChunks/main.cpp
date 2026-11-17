#include "Work.h"
#include "../Acceleration/Kernel.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <numeric>

int main() {
    using namespace ii;
    try {
        auto device=example::take(gpu::Device::create());
        const auto limits=device.limits();
        constexpr std::uint32_t group_size=64; // Must match the supplied SPIR-V shader.
        if(limits.max_group_size.x<group_size || limits.max_threads_per_group<group_size ||
           !limits.max_groups.x || limits.max_buffer_bytes<sizeof(std::uint32_t))
            throw std::runtime_error("Device cannot run the example shader");
        auto pipeline=example::take(device.compute_pipeline(example::transform()));
        const auto chunk_items=static_cast<std::uint32_t>(std::min<std::uint64_t>(256,limits.max_buffer_bytes/4));
        std::vector<std::uint32_t> input(4096),output(input.size());
        std::iota(input.begin(),input.end(),0u);
        runtime::ChunkQueue queue({.cpu_workers=2,.capacity=8,
            .devices={{.group_size=group_size,.max_groups=limits.max_groups.x}}});
        auto task=queue.submit(example::transform_work(device,pipeline,input,output,chunk_items));
        if(!task)throw std::runtime_error(task.error().message);
        auto state=task->snapshot();
        while(state.phase!=runtime::ChunkPhase::completed && state.phase!=runtime::ChunkPhase::failed &&
              state.phase!=runtime::ChunkPhase::cancelled)
            state=task->wait_for_change(state.revision,std::chrono::seconds(30));
        if(state.phase!=runtime::ChunkPhase::completed)throw std::runtime_error("Chunk task did not complete");
        for(std::size_t i=0;i<input.size();++i)if(output[i]!=input[i]*7+13)return 2;
        std::cout<<"items="<<state.completed_items<<" device_submissions="<<state.device_submitted<<'\n';
        if(auto result=queue.close();!result)throw std::runtime_error(result.error().message);
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
