#include <iiGPU/ComputeManager.h>
#include "../src/Backends/Backend.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iostream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <thread>

using namespace ii::gpu;
using namespace ii::gpu::detail;

namespace {
void check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class T> T take(Result<T> value) {
    if (!value) throw std::runtime_error(value.error().message);
    return std::move(*value);
}
template<class T> void rejects(const Result<T>& value, ErrorCode code, const char* message) {
    check(!value && value.error().code == code, message);
}

struct Gate {
    std::mutex mutex;
    std::condition_variable cv;
    unsigned arrivals = 0;
    bool parallel = false;
};
struct WaitGate {
    std::mutex mutex;
    std::condition_variable cv;
    bool entered = false, release = false;
};
struct Counts {
    std::atomic<unsigned> pipelines = 0, submissions = 0;
    std::atomic<unsigned> reads = 0;
    std::atomic<bool> fail = false;
    std::shared_ptr<Gate> gate;
    std::shared_ptr<WaitGate> wait_gate;
    std::uint64_t allocation_overhead = 0;
    std::mutex thread_mutex;
    std::thread::id pipeline_thread, submit_thread, read_thread;
};
struct FakeBuffer final : NativeBuffer {
    FakeBuffer(std::size_t size, std::shared_ptr<Counts> value) : bytes(size), counts(std::move(value)) {}
    std::vector<std::byte> bytes;
    std::shared_ptr<Counts> counts;
    void upload(std::span<const std::byte> data, std::uint64_t offset) override {
        std::copy(data.begin(), data.end(), bytes.begin() + offset);
    }
    void read(std::span<std::byte> data, std::uint64_t offset) override {
        { std::lock_guard lock(counts->thread_mutex); counts->read_thread = std::this_thread::get_id(); }
        ++counts->reads;
        std::copy_n(bytes.begin() + offset, data.size(), data.begin());
    }
    std::uint64_t handle() const override { return 1; }
    std::uint64_t allocation_bytes() const override { return bytes.size() + counts->allocation_overhead; }
};
struct FakePipeline final : NativePipeline {};
struct FakeSubmission final : NativeSubmission {
    explicit FakeSubmission(std::shared_ptr<WaitGate> value) : gate(std::move(value)) {}
    std::shared_ptr<WaitGate> gate;
    bool wait(std::chrono::nanoseconds) override {
        if (gate) {
            std::unique_lock lock(gate->mutex);
            gate->entered = true;
            gate->cv.notify_all();
            gate->cv.wait(lock, [&] { return gate->release; });
        }
        return true;
    }
};
struct FakeDevice final : NativeDevice {
    std::shared_ptr<Counts> counts;
    FakeDevice(std::shared_ptr<Counts> value, std::size_t index, bool hardware)
        : counts(std::move(value)) {
        adapter = {Backend::metal, index, "fake", DeviceKind::integrated, hardware};
        limits.max_buffer_bytes = 128;
        limits.max_bindings = 8;
        limits.max_constants_bytes = 64;
        limits.max_groups = {2, 1, 1};
        limits.max_group_size = {8, 1, 1};
        limits.max_threads_per_group = 8;
        limits.binding_offset_alignment = 4;
    }
    std::unique_ptr<NativeBuffer> buffer(BufferDesc desc) override {
        return std::make_unique<FakeBuffer>(desc.bytes, counts);
    }
    std::unique_ptr<NativePipeline> pipeline(const ComputeDesc&) override {
        { std::lock_guard lock(counts->thread_mutex); counts->pipeline_thread = std::this_thread::get_id(); }
        ++counts->pipelines;
        return std::make_unique<FakePipeline>();
    }
    std::shared_ptr<NativeSubmission> submit(const std::vector<Command>& commands) override {
        { std::lock_guard lock(counts->thread_mutex); counts->submit_thread = std::this_thread::get_id(); }
        if (counts->fail.exchange(false)) throw Failure(ErrorCode::backend_failure, "injected GPU failure");
        if (counts->gate && counts->submissions == 0) {
            std::unique_lock lock(counts->gate->mutex);
            ++counts->gate->arrivals;
            counts->gate->cv.notify_all();
            if (!counts->gate->cv.wait_for(lock, std::chrono::seconds(5), [&] { return counts->gate->arrivals == 2; }))
                throw Failure(ErrorCode::backend_failure, "independent devices were not scheduled concurrently");
            counts->gate->parallel = true;
        }
        for (const auto& command : commands) if (const auto* d = std::get_if<Dispatch>(&command)) {
            check(d->bindings.size() == 2 && d->constants.size() == 20, "invalid manager shader ABI");
            std::array<std::uint32_t, 5> p{};
            std::memcpy(p.data(), d->constants.data(), sizeof(p));
            check(p[0] > 0 && p[0] <= 8 && d->groups.x == (p[0] + 3) / 4,
                  "chunk or group size is incorrect");
            auto* in = static_cast<FakeBuffer*>(d->bindings[0].buffer->native.get());
            auto* out = static_cast<FakeBuffer*>(d->bindings[1].buffer->native.get());
            const auto base = std::uint64_t(p[3]) | (std::uint64_t(p[4]) << 32);
            for (std::uint32_t i = 0; i < p[0]; ++i) {
                std::uint32_t value = 0;
                std::memcpy(&value, in->bytes.data() + i * 4, 4);
                value = value * p[1] + p[2] + static_cast<std::uint32_t>(base + i);
                std::memcpy(out->bytes.data() + i * 4, &value, 4);
            }
        }
        ++counts->submissions;
        return std::make_shared<FakeSubmission>(counts->wait_gate);
    }
    NativeContext context() const override { return {}; }
};
Device device(const std::shared_ptr<Counts>& counts, std::size_t index, bool hardware = true,
              std::uint64_t budget = 128) {
    auto state = std::make_shared<DeviceState>();
    state->options.memory_budget = budget;
    state->native = std::make_unique<FakeDevice>(counts, index, hardware);
    return DeviceAccess::adopt(std::move(state));
}
ComputeProgram program() {
    ComputeProgram p;
    p.kernel.metal_source = "fake shader";
    p.kernel.bindings = {Access::read, Access::write};
    p.kernel.constants_bytes = 20;
    p.kernel.group_size = {4, 1, 1};
    p.input_stride = p.output_stride = sizeof(std::uint32_t);
    std::array<std::uint32_t, 2> params{7, 13};
    auto bytes = std::as_bytes(std::span(params));
    p.constants.assign(bytes.begin(), bytes.end());
    return p;
}

void parallel_partition_and_readback() {
    auto first = std::make_shared<Counts>(), second = std::make_shared<Counts>();
    first->gate = second->gate = std::make_shared<Gate>();
    auto manager = take(ComputeManager::from_devices({device(first, 0), device(second, 1)},
                                                       {.max_items_per_chunk = 3}));
    std::array<std::uint32_t, 19> input{};
    std::iota(input.begin(), input.end(), 1);
    auto result = take(manager->execute(program(), std::as_bytes(std::span(input))));
    check(result.bytes.size() == sizeof(input), "host result has incorrect size");
    for (std::size_t i = 0; i < input.size(); ++i) {
        std::uint32_t value = 0;
        std::memcpy(&value, result.bytes.data() + i * 4, 4);
        check(value == input[i] * 7 + 13 + i, "incorrect data, ordering, or global offset");
    }
    check(first->gate->parallel && first->submissions > 0 && second->submissions > 0,
          "work was not distributed to both devices");
    check(first->pipelines == 1 && second->pipelines == 1, "pipelines were not reused for chunks");
    check(result.devices.size() == 2 && result.devices[0].elements == 10 && result.devices[1].elements == 9 &&
          result.devices[0].chunks == 4 && result.devices[1].chunks == 3, "device report is incorrect");
}

void validation_and_errors() {
    auto counts = std::make_shared<Counts>();
    rejects(ComputeManager::from_devices({}), ErrorCode::invalid_argument, "empty manager accepted");
    auto same = device(counts, 0);
    rejects(ComputeManager::from_devices({same, same}), ErrorCode::invalid_argument, "duplicate device accepted");
    rejects(ComputeManager::from_devices({same, device(counts, 2)}, {.max_devices = 1}),
            ErrorCode::limit_exceeded, "device count limit was ignored");
    check(bool(ComputeManager::from_devices({same}, {.memory_budget_per_device = 0})),
          "existing devices must retain their own budgets");
    rejects(ComputeManager::discover(static_cast<Backend>(42)), ErrorCode::unsupported,
            "unknown backend was accepted");
    rejects(ComputeManager::discover(Backend::automatic, {.memory_budget_per_device = 0}),
            ErrorCode::invalid_argument, "zero discovery budget was accepted");
    rejects(ComputeManager::from_devices({device(counts, 1, false)}), ErrorCode::unavailable,
            "software device accepted as physical GPU");
    rejects(ComputeManager::from_devices({same}, {.max_items_per_chunk = 0}), ErrorCode::invalid_argument,
            "unbounded chunk size accepted");
    auto manager = take(ComputeManager::from_devices({same}, {.max_items_per_chunk = 3}));
    std::array<std::uint32_t, 4> input{1, 2, 3, 4};
    auto p = program();
    p.input_stride = 0;
    rejects(manager->execute(p, std::as_bytes(std::span(input))), ErrorCode::invalid_argument, "zero stride accepted");
    p = program(); p.kernel.bindings = {Access::write, Access::write};
    rejects(manager->execute(p, std::as_bytes(std::span(input))), ErrorCode::invalid_argument, "invalid binding ABI accepted");
    p = program(); p.constants.push_back(std::byte{});
    rejects(manager->execute(p, std::as_bytes(std::span(input))), ErrorCode::invalid_argument, "unaligned constants accepted");
    p = program(); p.kernel.constants_bytes = 24;
    rejects(manager->execute(p, std::as_bytes(std::span(input))), ErrorCode::invalid_argument, "incorrect constants size accepted");
    p = program(); p.kernel.group_size.y = 2;
    rejects(manager->execute(p, std::as_bytes(std::span(input))), ErrorCode::invalid_argument, "non-1D kernel accepted");
    p = program();
    rejects(manager->execute(p, std::as_bytes(std::span(input)).first(3)), ErrorCode::invalid_argument, "partial element accepted");
    std::stop_source stopped; stopped.request_stop();
    rejects(manager->execute(p, std::as_bytes(std::span(input)), stopped.get_token()), ErrorCode::cancelled,
            "pre-cancelled work was executed");
    auto empty = take(manager->execute(p, {}));
    check(empty.bytes.empty() && empty.devices[0].elements == 0 && counts->pipelines == 0,
          "empty input allocated GPU resources");
    counts->fail = true;
    rejects(manager->execute(p, std::as_bytes(std::span(input))), ErrorCode::backend_failure,
            "GPU failure was hidden or returned partial output");
    auto constrained = take(ComputeManager::from_devices({device(std::make_shared<Counts>(), 0, true, 7)}));
    rejects(constrained->execute(p, std::as_bytes(std::span(input))), ErrorCode::limit_exceeded,
            "insufficient GPU budget was ignored");
}

void limits_and_submitted_cancellation() {
    auto counts = std::make_shared<Counts>();
    auto limited = device(counts, 0, true, 16);
    auto manager = take(ComputeManager::from_devices({limited}, {.max_items_per_chunk = 100}));
    std::array<std::uint32_t, 5> input{1, 2, 3, 4, 5};
    auto result = take(manager->execute(program(), std::as_bytes(std::span(input))));
    check(result.devices[0].chunks == 3 && result.devices[0].elements == 5 &&
          limited.stats().allocated_bytes == 0, "buffer budget did not bound and release chunks");
    auto group_counts = std::make_shared<Counts>();
    auto group_manager = take(ComputeManager::from_devices({device(group_counts, 1)}, {.max_items_per_chunk = 100}));
    std::array<std::uint32_t, 21> many{};
    auto grouped = take(group_manager->execute(program(), std::as_bytes(std::span(many))));
    check(grouped.devices[0].chunks == 3 && grouped.devices[0].elements == 21,
          "device workgroup limit did not bound chunks");
    auto aligned_counts = std::make_shared<Counts>();
    aligned_counts->allocation_overhead = 8;
    auto aligned_device = device(aligned_counts, 2, true, 32);
    auto aligned = take(ComputeManager::from_devices({aligned_device}, {.max_items_per_chunk = 100}));
    std::array<std::uint32_t, 6> aligned_input{};
    auto aligned_result = take(aligned->execute(program(), std::as_bytes(std::span(aligned_input))));
    check(aligned_result.devices[0].chunks == 3 && aligned_device.stats().allocated_bytes == 0,
          "aligned native allocations were not retried within the budget");
    counts->wait_gate = std::make_shared<WaitGate>();
    std::stop_source stop;
    Result<ComputeResult> cancelled = std::unexpected(Error{ErrorCode::backend_failure, "not executed"});
    std::thread::id started_on, finished_on;
    std::thread caller([&] {
        started_on = std::this_thread::get_id();
        cancelled = manager->execute(program(), std::as_bytes(std::span(input)), stop.get_token());
        finished_on = std::this_thread::get_id();
    });
    bool started = false;
    {
        std::unique_lock lock(counts->wait_gate->mutex);
        started = counts->wait_gate->cv.wait_for(lock, std::chrono::seconds(5),
                                                [&] { return counts->wait_gate->entered; });
        stop.request_stop();
        counts->wait_gate->release = true;
        counts->wait_gate->cv.notify_all();
    }
    caller.join();
    check(started, "GPU submission did not start");
    rejects(cancelled, ErrorCode::cancelled, "submitted cancellation returned successful partial data");
    check(started_on == finished_on, "cancelled task did not return to its host thread");
    check(limited.stats().allocated_bytes == 0 && limited.stats().in_flight == 0,
          "cancellation did not wait for GPU resource retirement");
}

void host_task_owns_start_and_completion() {
    auto counts = std::make_shared<Counts>();
    counts->wait_gate = std::make_shared<WaitGate>();
    auto host_device = device(counts, 0);
    auto manager = take(ComputeManager::from_devices({host_device}));
    std::array<std::uint32_t, 1> input{5};
    std::thread::id started_on, finished_on;
    std::atomic<int> phase{0};
    Result<ComputeResult> result = std::unexpected(Error{ErrorCode::backend_failure, "not executed"});
    std::thread host_task([&] {
        started_on = std::this_thread::get_id();
        phase.store(1);
        result = manager->execute(program(), std::as_bytes(std::span(input)));
        finished_on = std::this_thread::get_id();
        phase.store(2);
    });
    bool submitted = false, remained_on_host = false;
    {
        std::unique_lock lock(counts->wait_gate->mutex);
        submitted = counts->wait_gate->cv.wait_for(lock, std::chrono::seconds(5),
                                                  [&] { return counts->wait_gate->entered; });
        remained_on_host = phase.load() == 1 && counts->reads == 0;
        counts->wait_gate->release = true;
        counts->wait_gate->cv.notify_all();
    }
    host_task.join();
    check(submitted && remained_on_host, "host task completed or read results before GPU completion");
    check(result && phase == 2 && counts->reads == 1, "host task did not receive the completed result");
    check(started_on == finished_on && started_on == counts->pipeline_thread,
          "task preparation and completion did not use the calling host thread");
    check(counts->submit_thread != started_on && counts->submit_thread == counts->read_thread,
          "per-device host work did not submit and read back inside the task lifetime");
    check(host_device.stats().in_flight == 0 && host_device.stats().allocated_bytes == 0,
          "host task returned before submitted GPU resources were retired");
    counts->wait_gate.reset();
    counts->fail = true;
    Result<ComputeResult> failure = std::unexpected(Error{ErrorCode::invalid_argument, "not executed"});
    std::thread::id failed_start, failed_finish;
    std::thread failure_task([&] {
        failed_start = std::this_thread::get_id();
        failure = manager->execute(program(), std::as_bytes(std::span(input)));
        failed_finish = std::this_thread::get_id();
    });
    failure_task.join();
    rejects(failure, ErrorCode::backend_failure, "GPU failure did not reach the host task");
    check(failed_start == failed_finish && host_device.stats().in_flight == 0 &&
          host_device.stats().allocated_bytes == 0, "failed task did not return and clean up on the host");
}
}

int main() {
    try { parallel_partition_and_readback(); validation_and_errors(); limits_and_submitted_cancellation();
          host_task_owns_start_and_completion(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
