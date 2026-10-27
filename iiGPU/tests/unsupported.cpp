#include <iiGPU/GPU.h>
int main() {
    using namespace ii::gpu;
    if(!Device::compiled_backends().empty()) return 1;
    for(auto api:{Backend::automatic,Backend::metal,Backend::vulkan}) {
        auto d=Device::create({.backend=api});
        if(d||d.error().code!=ErrorCode::unsupported) return 2;
    }
    auto invalid=Device::create({.memory_budget=0});
    if(invalid||invalid.error().code!=ErrorCode::invalid_argument) return 3;
    auto adapters=Device::adapters(Backend::vulkan);
    if(adapters||adapters.error().code!=ErrorCode::unsupported) return 4;
    return 0;
}
