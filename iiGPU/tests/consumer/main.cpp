#include <iiGPU/GPU.h>
#include <array>

int main() {
    auto device=ii::gpu::Device::create(); if(!device) return 1;
    auto buffer=device->buffer({256}); if(!buffer) return 2;
    auto commands=device->commands();
    if(!commands.fill(*buffer,0x01234567u)||!commands.finish()) return 3;
    auto job=device->submit(std::move(commands)); if(!job||!job->wait()) return 4;
    std::array<std::uint32_t,64> result{};
    if(!buffer->read(std::as_writable_bytes(std::span(result)))) return 5;
    for(auto value:result) if(value!=0x01234567u) return 6;
    return 0;
}
