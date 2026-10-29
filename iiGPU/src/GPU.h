#pragma once
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <span>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

namespace ii::gpu {
enum class Backend { automatic, metal, vulkan };
enum class DeviceKind { integrated, discrete, virtual_gpu, cpu, unknown };
enum class ErrorCode { invalid_argument, unavailable, unsupported, wrong_device,
    wrong_state, busy, limit_exceeded, timeout, cancelled, out_of_memory, backend_failure };
struct Error final { ErrorCode code; std::string message; };
template<class T> using Result = std::expected<T, Error>;
struct Extent final {
    std::uint32_t x=1, y=1, z=1;
    friend bool operator==(Extent, Extent) = default;
};
struct Adapter final {
    Backend backend=Backend::automatic;
    std::size_t index=0;
    std::string name;
    DeviceKind kind=DeviceKind::unknown;
    bool hardware=false;
};
struct Limits final {
    std::uint64_t max_buffer_bytes=0;
    std::uint32_t max_bindings=0, max_constants_bytes=0;
    Extent max_groups{}, max_group_size{};
    std::uint32_t max_threads_per_group=0;
    std::uint64_t binding_offset_alignment=1;
};
struct Options final {
    Backend backend=Backend::automatic;
    std::size_t adapter_index=0;
    bool require_hardware=true;
    std::uint64_t memory_budget=256u*1024u*1024u;
    std::size_t max_in_flight=16, max_commands=4096;
};
enum class Memory { shared, device };
enum class Usage : std::uint32_t { storage=1, transfer_source=2, transfer_destination=4 };
constexpr Usage operator|(Usage a, Usage b) noexcept {
    return static_cast<Usage>(static_cast<std::uint32_t>(a)|static_cast<std::uint32_t>(b));
}
struct BufferDesc final {
    std::uint64_t bytes=0;
    Memory memory=Memory::shared;
    Usage usage=Usage::storage|Usage::transfer_source|Usage::transfer_destination;
};
enum class Access { read, write, read_write };
// Bindings are consecutive storage-buffer slots, starting at zero. Metal
// constants occupy slot bindings.size(); Vulkan constants use push constants.
struct ComputeDesc final {
    std::string metal_source, metal_entry="main0";
    std::vector<std::byte> metal_library;
    std::vector<std::uint32_t> spirv;
    std::string vulkan_entry="main";
    std::vector<Access> bindings;
    std::uint32_t constants_bytes=0;
    Extent group_size{64,1,1};
};
struct Stats final {
    std::uint64_t allocated_bytes=0, submissions=0, completed=0, failed=0;
    std::size_t in_flight=0;
};
namespace detail {
struct DeviceState; struct BufferState; struct PipelineState;
struct CommandsState; struct SubmissionState;
}
class Device;
class CommandList;
class Buffer final {
public:
    Buffer()=default;
    explicit operator bool() const noexcept { return bool(state_); }
    std::uint64_t size() const noexcept;
    Result<void> upload(std::span<const std::byte>, std::uint64_t offset=0) const;
    Result<void> read(std::span<std::byte>, std::uint64_t offset=0) const;
private:
    friend class Device; friend class CommandList;
    explicit Buffer(std::shared_ptr<detail::BufferState> state):state_(std::move(state)) {}
    std::shared_ptr<detail::BufferState> state_;
};
struct Binding final {
    Buffer buffer;
    std::uint64_t offset=0, bytes=0; // Zero bytes selects the remaining range.
};
class ComputePipeline final {
public:
    ComputePipeline()=default;
    explicit operator bool() const noexcept { return bool(state_); }
private:
    friend class Device; friend class CommandList;
    explicit ComputePipeline(std::shared_ptr<detail::PipelineState> state):state_(std::move(state)) {}
    std::shared_ptr<detail::PipelineState> state_;
};
class CommandList final {
public:
    CommandList();
    ~CommandList();
    CommandList(CommandList&&) noexcept;
    CommandList& operator=(CommandList&&) noexcept;
    CommandList(const CommandList&)=delete;
    CommandList& operator=(const CommandList&)=delete;
    Result<void> copy(const Buffer& source, const Buffer& destination,
        std::uint64_t bytes, std::uint64_t source_offset=0, std::uint64_t destination_offset=0);
    Result<void> fill(const Buffer&, std::uint32_t pattern, std::uint64_t offset=0, std::uint64_t bytes=0);
    Result<void> dispatch(const ComputePipeline&, std::span<const Binding>, Extent groups,
        std::span<const std::byte> constants={});
    // A GPU memory dependency between earlier writes/reads and later commands.
    Result<void> barrier();
    Result<void> finish();
private:
    friend class Device;
    explicit CommandList(std::unique_ptr<detail::CommandsState>);
    std::unique_ptr<detail::CommandsState> state_;
};
class Submission final {
public:
    Submission()=default;
    explicit operator bool() const noexcept { return bool(state_); }
    Result<bool> poll() const;
    Result<void> wait(std::chrono::nanoseconds timeout=std::chrono::nanoseconds::max()) const;
    // Submitted GPU work cannot be force-cancelled. The final handle's release
    // waits for native completion to preserve referenced resources.
private:
    friend class Device;
    explicit Submission(std::shared_ptr<detail::SubmissionState> state):state_(std::move(state)) {}
    std::shared_ptr<detail::SubmissionState> state_;
};
// Borrowed native handles are valid only inside native_access's callback.
// Native work must complete before returning; the callback must not reenter
// this device. Vendor types/headers remain in the consuming adapter.
struct NativeContext final {
    Backend backend=Backend::automatic;
    std::uint64_t instance=0, physical_device=0, device=0, queue=0;
    std::uint32_t queue_family=0;
    std::vector<std::uint64_t> buffers;
};
class Device final {
public:
    Device()=default;
    static Result<Device> create(Options={});
    static Result<std::vector<Adapter>> adapters(Backend);
    static std::vector<Backend> compiled_backends();
    explicit operator bool() const noexcept { return bool(state_); }
    Adapter adapter() const;
    Limits limits() const;
    Stats stats() const;
    Result<Buffer> buffer(BufferDesc) const;
    Result<ComputePipeline> compute_pipeline(const ComputeDesc&) const;
    CommandList commands() const;
    Result<Submission> submit(CommandList&&, std::stop_token={}) const;
    Result<void> collect() const;
    Result<void> native_access(std::span<const Buffer>,
        const std::function<Result<void>(const NativeContext&)>&) const;
private:
    explicit Device(std::shared_ptr<detail::DeviceState> state):state_(std::move(state)) {}
    std::shared_ptr<detail::DeviceState> state_;
};
const char* backend_name(Backend) noexcept;
} // namespace ii::gpu
