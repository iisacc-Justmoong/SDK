#pragma once

#include "GPU.h"

namespace ii::gpu {

struct ComputeManagerOptions final {
    std::size_t max_devices = 8;
    std::uint32_t max_items_per_chunk = 65536;
    std::uint64_t memory_budget_per_device = 256u * 1024u * 1024u;
};

// The kernel reads one input and writes every byte of one output per element.
// Constants contain count, user bytes, then the low/high words of global start.
struct ComputeProgram final {
    ComputeDesc kernel;
    std::size_t input_stride = 0;
    std::size_t output_stride = 0;
    std::vector<std::byte> constants;
};

struct ComputeDeviceReport final {
    Adapter adapter;
    std::uint64_t elements = 0;
    std::uint64_t chunks = 0;
};

struct ComputeResult final {
    std::vector<std::byte> bytes;
    std::vector<ComputeDeviceReport> devices;
};

class ComputeManager final {
public:
    static Result<std::unique_ptr<ComputeManager>> discover(
        Backend = Backend::automatic, ComputeManagerOptions = {});
    static Result<std::unique_ptr<ComputeManager>> from_devices(
        std::vector<Device>, ComputeManagerOptions = {});
    ~ComputeManager();
    ComputeManager(const ComputeManager&) = delete;
    ComputeManager& operator=(const ComputeManager&) = delete;

    std::vector<Adapter> adapters() const;
    // The caller's host task owns the lifecycle. This synchronous call waits
    // for GPU completion and host readback, then returns on the calling thread.
    // Cancellation cannot preempt work already submitted to a native GPU queue.
    Result<ComputeResult> execute(const ComputeProgram&, std::span<const std::byte>,
                                 std::stop_token = {});
private:
    struct Impl;
    explicit ComputeManager(std::unique_ptr<Impl>);
    std::unique_ptr<Impl> impl_;
};
} // namespace ii::gpu
