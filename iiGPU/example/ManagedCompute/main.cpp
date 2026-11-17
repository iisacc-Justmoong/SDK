#include <iiGPU/ComputeManager.h>
#include "../Acceleration/Kernel.h"
#include <array>
#include <cstring>
#include <iostream>
#include <numeric>

namespace {
using Values = std::array<std::uint32_t, 1003>;

Values begin_host_task() {
    Values input{};
    std::iota(input.begin(), input.end(), 0u);
    return input;
}

bool finish_host_task(const Values& input, const ii::gpu::ComputeResult& result) {
    for (std::size_t i = 0; i < input.size(); ++i) {
        std::uint32_t value = 0;
        std::memcpy(&value, result.bytes.data() + i * sizeof(value), sizeof(value));
        if (value != input[i] * 7 + 13) return false;
    }
    for (const auto& report : result.devices)
        std::cout << ii::gpu::backend_name(report.adapter.backend) << " " << report.adapter.name
                  << " elements=" << report.elements << " chunks=" << report.chunks << '\n';
    return true;
}
}

int main(int argc, char** argv) {
    using namespace ii::gpu;
    try {
        auto backend = Backend::automatic;
        if (argc > 1) {
            const std::string choice(argv[1]);
            if (choice == "metal") backend = Backend::metal;
            else if (choice == "vulkan") backend = Backend::vulkan;
            else throw std::invalid_argument("expected metal or vulkan");
        }
        auto input = begin_host_task();
        auto manager = example::take(ComputeManager::discover(backend, {.max_items_per_chunk = 67}));
        ComputeProgram program;
        program.kernel = example::transform();
        program.kernel.constants_bytes += 8; // Manager appends the global offset words.
        program.input_stride = program.output_stride = sizeof(std::uint32_t);
        const std::array<std::uint32_t, 2> parameters{7, 13};
        auto bytes = std::as_bytes(std::span(parameters));
        program.constants.assign(bytes.begin(), bytes.end());
        // Only the data-parallel computation is submitted to the GPU.
        auto result = example::take(manager->execute(program, std::as_bytes(std::span(input))));
        if (!finish_host_task(input, result)) return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
