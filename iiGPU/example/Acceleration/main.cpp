#include "Kernel.h"
#include <array>
#include <iostream>
#include <numeric>

int main(int argc,char** argv) {
    using namespace ii::gpu;
    try {
        Options options;
        if(argc>1) options.backend=std::string(argv[1])=="vulkan"?Backend::vulkan:Backend::metal;
        auto device=example::take(Device::create(options));
        auto pipeline=example::take(device.compute_pipeline(example::transform()));
        std::array<std::uint32_t,257> input{},output{};
        std::iota(input.begin(),input.end(),0u);
        auto source=example::take(device.buffer({sizeof(input)}));
        auto destination=example::take(device.buffer({sizeof(output)}));
        example::take(source.upload(std::as_bytes(std::span(input))));
        std::array<Binding,2> buffers{{{source},{destination}}};
        std::array<std::uint32_t,3> parameters{257,7,13};
        auto commands=device.commands();
        example::take(commands.dispatch(pipeline,buffers,{5,1,1},std::as_bytes(std::span(parameters))));
        example::take(commands.finish());
        auto submission=example::take(device.submit(std::move(commands)));
        example::take(submission.wait());
        example::take(destination.read(std::as_writable_bytes(std::span(output))));
        for(std::size_t i=0;i<input.size();++i) if(output[i]!=input[i]*7+13) return 2;
        std::cout<<backend_name(device.adapter().backend)<<" adapter="<<device.adapter().name
            <<" hardware="<<device.adapter().hardware<<" completed="<<device.stats().completed
            <<" values="<<output.size()<<" first="<<output.front()<<" last="<<output.back()<<'\n';
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
