#include <iiWebGPU/Memory.h>

#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ii::webgpu;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main() {
    try {
        MemoryBudget budget(1024);
        {
            auto a = budget.reserve(768);
            require(a.has_value(), "reservation failed");
            auto copy = *a;
            require(budget.stats().used == 768, "copy charged twice");
            require(!budget.reserve(257), "budget exceeded");
            require(!budget.set_limit(767), "live reservations invalidated");
            require(budget.set_limit(2048).has_value(), "budget increase failed");
            a = std::unexpected(Error{ErrorCode::Cancelled, "release original"});
            require(budget.stats().used == 768, "shared lease released too early");
        }
        require(budget.stats().used == 0 && budget.stats().peak == 768, "lease did not release");
        require(!budget.reserve(0), "zero reservation accepted");
        MemoryBudget huge(std::numeric_limits<std::uint64_t>::max());
        auto almost = huge.reserve(std::numeric_limits<std::uint64_t>::max()-3);
        require(almost.has_value() && !huge.reserve(4), "budget overflow");

        RangeAllocator ranges(1024);
        {
            auto a = ranges.allocate(100, 256);
            auto b = ranges.allocate(100, 256);
            require(a && b && a->offset() == 0 && b->offset() == 256, "alignment");
            auto copy = *a;
            a = std::unexpected(Error{ErrorCode::Cancelled, "release"});
            require(ranges.stats().used == 200, "range copy released early");
            require(!ranges.allocate(1, 3), "non-power-of-two alignment");
            require(!ranges.allocate(0, 1), "zero range");
            require(!ranges.allocate(1025, 1), "range overflow");
        }
        require(ranges.stats().used == 0 && ranges.stats().largest_free == 1024, "free ranges did not coalesce");
        auto full = ranges.allocate(1024, 1);
        require(full && !ranges.allocate(1, 1), "full capacity");
        full = std::unexpected(Error{ErrorCode::Cancelled, "release"});

        MemoryBudget concurrent(64);
        RangeAllocator shared(4096);
        std::atomic<bool> failed = false;
        std::vector<std::thread> threads;
        for (int t=0; t<8; ++t) threads.emplace_back([&] {
            for (int n=0; n<2000; ++n) {
                auto lease = concurrent.reserve(8);
                auto range = shared.allocate(32, 16);
                if (!lease || !range) failed = true;
            }
        });
        for (auto& thread : threads) thread.join();
        require(!failed && concurrent.stats().used == 0 && shared.stats().used == 0, "concurrent lease accounting");
        require(shared.stats().largest_free == 4096, "concurrent fragmentation");
        std::cout << "memory contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
