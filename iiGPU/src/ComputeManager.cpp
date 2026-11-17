#include "ComputeManager.h"
#include "Backends/Backend.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cstring>
#include <exception>
#include <limits>
#include <mutex>
#include <thread>

namespace ii::gpu {
using namespace detail;

namespace {
template<class T> T checked(Result<T> result) {
    if (!result) throw Failure(result.error().code, result.error().message);
    return std::move(*result);
}
void checked(Result<void> result) {
    if (!result) throw Failure(result.error().code, result.error().message);
}
void validate_options(const ComputeManagerOptions& options) {
    require(options.max_devices && options.max_items_per_chunk,
            ErrorCode::invalid_argument, "compute manager limits must be positive");
}
}

struct ComputeManager::Impl final {
    std::vector<Device> devices;
    ComputeManagerOptions options;
    std::mutex execution;
};

ComputeManager::ComputeManager(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ComputeManager::~ComputeManager() = default;

Result<std::unique_ptr<ComputeManager>> ComputeManager::from_devices(
        std::vector<Device> devices, ComputeManagerOptions options) {
    return protect([&] {
        validate_options(options);
        require(!devices.empty(), ErrorCode::invalid_argument, "compute manager requires at least one device");
        require(devices.size() <= options.max_devices, ErrorCode::limit_exceeded, "too many compute devices");
        std::vector<Adapter> seen;
        for (const auto& device : devices) {
            require(bool(device), ErrorCode::invalid_argument, "empty compute device");
            auto adapter = device.adapter();
            require(adapter.hardware, ErrorCode::unavailable, "compute manager requires physical GPU devices");
            for (const auto& previous : seen)
                require(previous.backend != adapter.backend || previous.index != adapter.index,
                        ErrorCode::invalid_argument, "duplicate compute device");
            seen.push_back(std::move(adapter));
        }
        auto impl = std::make_unique<Impl>();
        impl->devices = std::move(devices);
        impl->options = options;
        return std::unique_ptr<ComputeManager>(new ComputeManager(std::move(impl)));
    });
}

Result<std::unique_ptr<ComputeManager>> ComputeManager::discover(Backend backend, ComputeManagerOptions options) {
    return protect([&] {
        validate_options(options);
        require(options.memory_budget_per_device, ErrorCode::invalid_argument, "compute device budget must be positive");
        const auto backends = backend == Backend::automatic ? Device::compiled_backends() : std::vector{backend};
        require(!backends.empty(), ErrorCode::unsupported, "no GPU backend was compiled");
        std::optional<Error> first_error;
        for (auto api : backends) {
            auto found = Device::adapters(api);
            if (!found) {
                if (!first_error) first_error = found.error();
                continue;
            }
            std::vector<Device> devices;
            for (const auto& adapter : *found) {
                if (!adapter.hardware) continue;
                if (devices.size() == options.max_devices) break;
                Options device_options;
                device_options.backend = api;
                device_options.adapter_index = adapter.index;
                device_options.memory_budget = options.memory_budget_per_device;
                devices.push_back(checked(Device::create(device_options)));
            }
            if (!devices.empty()) return checked(from_devices(std::move(devices), options));
        }
        if (first_error) throw Failure(first_error->code, first_error->message);
        fail(ErrorCode::unavailable, "no physical GPU device is available");
    });
}

std::vector<Adapter> ComputeManager::adapters() const {
    std::vector<Adapter> result;
    result.reserve(impl_->devices.size());
    for (const auto& device : impl_->devices) result.push_back(device.adapter());
    return result;
}

Result<ComputeResult> ComputeManager::execute(const ComputeProgram& program,
        std::span<const std::byte> input, std::stop_token stop) {
    return protect([&] {
        std::lock_guard execution(impl_->execution);
        require(!stop.stop_requested(), ErrorCode::cancelled, "compute execution was cancelled");
        require(std::endian::native == std::endian::little, ErrorCode::unsupported,
                "compute manager constants require a little-endian host");
        require(program.input_stride && program.output_stride, ErrorCode::invalid_argument, "compute strides must be positive");
        require(input.size() % program.input_stride == 0, ErrorCode::invalid_argument, "input contains a partial element");
        require(program.kernel.bindings.size() == 2 && program.kernel.bindings[0] == Access::read &&
                program.kernel.bindings[1] == Access::write,
                ErrorCode::invalid_argument, "compute manager requires read and write buffer bindings");
        require(program.constants.size() % 4 == 0 &&
                program.constants.size() <= (std::numeric_limits<std::uint32_t>::max)() - 12u &&
                program.kernel.constants_bytes == program.constants.size() + 12u,
                ErrorCode::invalid_argument, "compute constants do not match the manager ABI");
        require(program.kernel.group_size.x && program.kernel.group_size.y == 1 && program.kernel.group_size.z == 1,
                ErrorCode::invalid_argument, "compute manager requires a one-dimensional workgroup");
        const auto count = input.size() / program.input_stride;
        require(count <= (std::numeric_limits<std::size_t>::max)() / program.output_stride,
                ErrorCode::limit_exceeded, "host output size overflows");
        require(program.input_stride <= (std::numeric_limits<std::uint64_t>::max)() - program.output_stride,
                ErrorCode::limit_exceeded, "GPU element size overflows");

        ComputeResult output;
        output.devices.reserve(impl_->devices.size());
        for (const auto& device : impl_->devices) output.devices.push_back({device.adapter()});
        output.bytes.resize(count * program.output_stride);
        if (!count) return output;

        struct Work final {
            Device device;
            ComputePipeline pipeline;
            Buffer input, output;
            std::size_t begin = 0, count = 0, capacity = 0, report = 0;
        };
        std::vector<Work> work;
        work.reserve(impl_->devices.size());
        const auto base = count / impl_->devices.size();
        const auto remainder = count % impl_->devices.size();
        std::size_t begin = 0;
        for (std::size_t i = 0; i < impl_->devices.size(); ++i) {
            const auto amount = base + (i < remainder ? 1u : 0u);
            if (!amount) continue;
            const auto& device = impl_->devices[i];
            const auto limits = device.limits();
            const auto budget = checked(device.memory_budget());
            const auto allocated = device.stats().allocated_bytes;
            require(allocated <= budget, ErrorCode::limit_exceeded, "device buffer budget is exhausted");
            const auto stride = static_cast<std::uint64_t>(program.input_stride) + program.output_stride;
            const auto max_groups = std::uint64_t(limits.max_groups.x) * program.kernel.group_size.x;
            auto capacity = std::min({std::uint64_t(amount), std::uint64_t(impl_->options.max_items_per_chunk),
                std::uint64_t((std::numeric_limits<std::uint32_t>::max)()), max_groups,
                limits.max_buffer_bytes / program.input_stride, limits.max_buffer_bytes / program.output_stride,
                (budget - allocated) / stride});
            require(capacity > 0, ErrorCode::limit_exceeded, "GPU limits or buffer budget cannot fit one element");
            Work item;
            item.device = device;
            item.pipeline = checked(device.compute_pipeline(program.kernel));
            // Native allocations may be larger than their requested buffers.
            // Retry smaller chunks without retaining either failed allocation.
            for (;;) {
                require(!stop.stop_requested(), ErrorCode::cancelled, "compute execution was cancelled");
                std::optional<Error> allocation_error;
                {
                    auto source = device.buffer({capacity * program.input_stride});
                    if (!source) allocation_error = source.error();
                    else {
                        auto destination = device.buffer({capacity * program.output_stride});
                        if (destination) {
                            item.input = std::move(*source);
                            item.output = std::move(*destination);
                            break;
                        }
                        allocation_error = destination.error();
                    }
                }
                if (capacity == 1 || (allocation_error->code != ErrorCode::limit_exceeded &&
                                      allocation_error->code != ErrorCode::out_of_memory))
                    throw Failure(allocation_error->code, allocation_error->message);
                capacity = std::max<std::uint64_t>(1, capacity / 2);
            }
            item.begin = begin;
            item.count = amount;
            item.capacity = static_cast<std::size_t>(capacity);
            item.report = i;
            work.push_back(std::move(item));
            begin += amount;
        }
        require(!stop.stop_requested(), ErrorCode::cancelled, "compute execution was cancelled");
        std::atomic<bool> failed = false;
        std::vector<std::exception_ptr> errors(work.size());
        std::vector<std::thread> threads;
        struct JoinThreads final {
            std::vector<std::thread>& threads;
            ~JoinThreads() { for (auto& thread : threads) if (thread.joinable()) thread.join(); }
        } joiner{threads};
        threads.reserve(work.size());
        try {
            for (std::size_t i = 0; i < work.size(); ++i) {
                threads.emplace_back([&, i] {
                    try {
                        auto& item = work[i];
                        std::vector<std::byte> constants(program.kernel.constants_bytes);
                        std::copy(program.constants.begin(), program.constants.end(), constants.begin() + 4);
                        const std::array<Binding, 2> bindings{{{item.input}, {item.output}}};
                        for (std::size_t position = 0; position < item.count;) {
                            if (failed.load()) return;
                            require(!stop.stop_requested(), ErrorCode::cancelled, "compute execution was cancelled");
                            const auto amount = std::min(item.capacity, item.count - position);
                            const auto global = std::uint64_t(item.begin + position);
                            const auto n = static_cast<std::uint32_t>(amount);
                            const auto low = static_cast<std::uint32_t>(global);
                            const auto high = static_cast<std::uint32_t>(global >> 32);
                            std::memcpy(constants.data(), &n, 4);
                            std::memcpy(constants.data() + 4 + program.constants.size(), &low, 4);
                            std::memcpy(constants.data() + 8 + program.constants.size(), &high, 4);
                            checked(item.input.upload(input.subspan((item.begin + position) * program.input_stride,
                                                                    amount * program.input_stride)));
                            auto commands = item.device.commands();
                            const auto groups = static_cast<std::uint32_t>((std::uint64_t(amount) +
                                program.kernel.group_size.x - 1) / program.kernel.group_size.x);
                            checked(commands.dispatch(item.pipeline, bindings, {groups, 1, 1}, constants));
                            checked(commands.finish());
                            auto submission = checked(item.device.submit(std::move(commands), stop));
                            checked(submission.wait());
                            require(!stop.stop_requested(), ErrorCode::cancelled, "compute execution was cancelled");
                            checked(item.output.read(std::span(output.bytes).subspan(
                                (item.begin + position) * program.output_stride, amount * program.output_stride)));
                            output.devices[item.report].elements += amount;
                            ++output.devices[item.report].chunks;
                            position += amount;
                        }
                    } catch (...) {
                        errors[i] = std::current_exception();
                        failed.store(true);
                    }
                });
            }
        } catch (...) {
            failed.store(true);
            throw;
        }
        for (auto& thread : threads) thread.join();
        for (const auto& error : errors) if (error) std::rethrow_exception(error);
        require(!stop.stop_requested(), ErrorCode::cancelled, "compute execution was cancelled");
        return output;
    });
}
} // namespace ii::gpu
