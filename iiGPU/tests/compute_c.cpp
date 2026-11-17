#include "../src/GPUResourceC.h"
#include <cstdint>

int main() {
    constexpr int invalid = 1;
    auto* manager = reinterpret_cast<iigpu_resource_manager*>(std::uintptr_t(1));
    if (iigpu_resource_manager_create(9, 1024, 1, 1, &manager) != invalid || manager) return 1;
    if (iigpu_resource_manager_create(0, 0, 1, 1, &manager) != invalid || manager) return 2;
    if (iigpu_resource_manager_create(0, 1024, 0, 1, &manager) != invalid || manager) return 3;
    if (iigpu_resource_manager_create(0, 1024, 1, 0, &manager) != invalid || manager) return 4;
    if (iigpu_resource_manager_count(nullptr, nullptr) != invalid) return 5;
    if (iigpu_resource_manager_execute(nullptr, nullptr, nullptr, 0, 1, nullptr, 0, 1) != invalid) return 6;
    iigpu_resource_manager_destroy(nullptr);
    return 0;
}
