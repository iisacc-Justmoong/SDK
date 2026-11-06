#pragma once

#include "Memory.h"
#include "Native.h"
#include "Texture.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace ii::webgpu {
namespace detail {
struct InstanceState; struct DeviceState; struct BufferState; struct TextureState;
struct BindingState; struct CommandState; struct MappingState;
void process_events(const std::shared_ptr<InstanceState>&);
Status wait_event(const std::shared_ptr<InstanceState>&, WGPUFuture, std::chrono::nanoseconds);
template<class T> struct AsyncState final {
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::optional<Result<T>> result;
    // Three error scopes and, for submissions, one queue-completion event.
    std::array<WGPUFuture,4> native_futures{};
    std::array<bool,4> observed{};
    std::size_t future_count=0;
    bool invalid_futures=false, native_wait_active=false;
};
template<class T> void complete(const std::shared_ptr<AsyncState<T>>& state, Result<T> result) {
    {
        std::lock_guard lock(state->mutex);
        if (!state->result) state->result.emplace(std::move(result));
    }
    state->changed.notify_all();
}
template<class T> void set_future(const std::shared_ptr<AsyncState<T>>& state, WGPUFuture future) {
    std::lock_guard lock(state->mutex);
    if (!future.id || state->future_count==state->native_futures.size()) state->invalid_futures=true;
    else state->native_futures[state->future_count++]=future;
}
}

// poll() is nonblocking. wait() is a native-host convenience; a browser host
// must yield to its event loop and then poll/result instead of blocking.
template<class T> class Future final {
public:
    Future() = default;
    Future(std::shared_ptr<detail::AsyncState<T>> state, std::shared_ptr<detail::InstanceState> instance,
            std::shared_ptr<void> owner = {})
        : state_(std::move(state)), instance_(std::move(instance)), owner_(std::move(owner)) {}
    [[nodiscard]] bool ready() const {
        if (!state_) return true;
        std::lock_guard lock(state_->mutex); return state_->result.has_value();
    }
    bool poll() const { detail::process_events(instance_); return ready(); }
    [[nodiscard]] Result<T> result() const {
        if (!state_) return std::unexpected(Error{ErrorCode::InvalidArgument, "empty future"});
        std::lock_guard lock(state_->mutex);
        if (!state_->result) return std::unexpected(Error{ErrorCode::NotReady, "operation is pending"});
        return *state_->result;
    }
    [[nodiscard]] Result<T> wait(std::chrono::nanoseconds timeout = std::chrono::seconds(10)) const {
        if (!state_) return std::unexpected(Error{ErrorCode::InvalidArgument,"empty future"});
        const auto start = std::chrono::steady_clock::now();
        const bool unlimited=timeout==std::chrono::nanoseconds::max();
        std::unique_lock lock(state_->mutex);
        for (;;) {
            if (state_->result) return *state_->result;
            const auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start);
            if (timeout<=std::chrono::nanoseconds::zero() || (!unlimited && elapsed>=timeout))
                return std::unexpected(Error{ErrorCode::Timeout,"wait timed out; GPU work was not cancelled"});
#ifdef __EMSCRIPTEN__
            return std::unexpected(Error{ErrorCode::Unsupported,"yield to the browser event loop and explicitly poll the future"});
#else
            const auto remaining=unlimited ? std::chrono::nanoseconds::max() : timeout-elapsed;
            if (state_->native_wait_active) {
                const auto changed=[&] { return state_->result.has_value() || !state_->native_wait_active; };
                if (unlimited) state_->changed.wait(lock,changed);
                else if (!state_->changed.wait_for(lock,remaining,changed))
                    return std::unexpected(Error{ErrorCode::Timeout,"wait timed out; GPU work was not cancelled"});
                continue;
            }
            if (state_->invalid_futures)
                return std::unexpected(Error{ErrorCode::Internal,"invalid native WebGPU future registration"});
            std::size_t index=0;
            while (index<state_->future_count && state_->observed[index]) ++index;
            if (index==state_->future_count)
                return std::unexpected(Error{ErrorCode::Internal,"native WebGPU completions did not resolve the future"});
            const auto future=state_->native_futures[index];
            state_->native_wait_active=true;
            lock.unlock();
            Status status;
            try { status=detail::wait_event(instance_,future,remaining); }
            catch (...) {
                lock.lock(); state_->native_wait_active=false; state_->changed.notify_all();
                throw;
            }
            lock.lock();
            state_->native_wait_active=false;
            if (status) state_->observed[index]=true;
            state_->changed.notify_all();
            if (state_->result) return *state_->result;
            if (!status) return std::unexpected(status.error());
#endif
        }
    }
private:
    std::shared_ptr<detail::AsyncState<T>> state_;
    std::shared_ptr<detail::InstanceState> instance_;
    // Only the user-visible future retains its device; callback cookies do not.
    std::shared_ptr<void> owner_;
};

class Adapter; class Device; class Buffer; class Texture; class Commands; class BufferArena;
class Context final {
public:
    Context() = default;
    [[nodiscard]] static Result<Context> create();
    [[nodiscard]] Future<Adapter> request_adapter(const WGPURequestAdapterOptions& = WGPU_REQUEST_ADAPTER_OPTIONS_INIT) const;
    [[nodiscard]] Result<Surface> create_surface(const WGPUSurfaceDescriptor&) const;
    void poll() const;
    [[nodiscard]] WGPUInstance native() const noexcept;
private:
    explicit Context(std::shared_ptr<detail::InstanceState> state) : state_(std::move(state)) {}
    std::shared_ptr<detail::InstanceState> state_;
};
struct AdapterInfo final {
    std::string vendor, architecture, device, description;
    std::uint32_t vendor_id = 0, device_id = 0;
    WGPUBackendType backend = WGPUBackendType_Undefined;
    WGPUAdapterType type = WGPUAdapterType_Unknown;
};
struct DeviceOptions final {
    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    std::uint64_t memory_budget = 256ull * 1024 * 1024;
};
class Adapter final {
public:
    Adapter() = default;
    [[nodiscard]] AdapterInfo info() const;
    [[nodiscard]] Result<WGPULimits> limits() const;
    [[nodiscard]] std::vector<WGPUFeatureName> features() const;
    [[nodiscard]] Future<Device> request_device(const DeviceOptions& = {}) const;
    [[nodiscard]] WGPUAdapter native() const noexcept { return handle_.native(); }
    Adapter(Handle<WGPUAdapter> handle, std::shared_ptr<detail::InstanceState> instance)
        : handle_(std::move(handle)), instance_(std::move(instance)) {}
private:
    Handle<WGPUAdapter> handle_;
    std::shared_ptr<detail::InstanceState> instance_;
};
struct Diagnostic final {
    ErrorCode code = ErrorCode::Internal;
    std::string message;
};
class BufferSlice final {
public:
    BufferSlice() = default;
    [[nodiscard]] WGPUBuffer native() const noexcept;
    [[nodiscard]] std::uint64_t offset() const noexcept { return offset_; }
    [[nodiscard]] std::uint64_t size() const noexcept { return size_; }
    [[nodiscard]] WGPUBufferUsage usage() const noexcept;
    [[nodiscard]] Result<BufferSlice> slice(std::uint64_t offset, std::uint64_t size) const;
private:
    std::shared_ptr<detail::BufferState> state_;
    RangeAllocation allocation_;
    std::uint64_t offset_ = 0, size_ = 0;
    friend class Buffer; friend class BufferArena; friend class Device; friend class Commands;
};
class Buffer final {
public:
    Buffer() = default;
    [[nodiscard]] WGPUBuffer native() const noexcept;
    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] WGPUBufferUsage usage() const noexcept;
    [[nodiscard]] Result<BufferSlice> slice(std::uint64_t offset, std::uint64_t size) const;
    [[nodiscard]] BufferSlice whole() const;
    explicit Buffer(std::shared_ptr<detail::BufferState> state) : state_(std::move(state)) {}
private:
    std::shared_ptr<detail::BufferState> state_;
    friend class Device; friend class BufferArena;
};
// Mapping copies share one mapping. Its final owner unmaps. Read-only mappings
// never expose a writable span; buffer offsets use WebGPU's 8/4-byte rules.
class Mapping final {
public:
    Mapping() = default;
    [[nodiscard]] std::span<const std::byte> bytes() const noexcept;
    [[nodiscard]] Result<std::span<std::byte>> writable_bytes() const;
    explicit Mapping(std::shared_ptr<detail::MappingState> state) : state_(std::move(state)) {}
private:
    std::shared_ptr<detail::MappingState> state_;
};
class Texture final {
public:
    Texture() = default;
    [[nodiscard]] WGPUTexture native() const noexcept;
    [[nodiscard]] std::uint64_t logical_bytes() const noexcept;
    [[nodiscard]] Future<TextureView> view(const WGPUTextureViewDescriptor* = nullptr) const;
    explicit Texture(std::shared_ptr<detail::TextureState> state) : state_(std::move(state)) {}
private:
    std::shared_ptr<detail::TextureState> state_;
    friend class Device; friend class Commands;
};
class BindGroup final {
public:
    BindGroup() = default;
    [[nodiscard]] WGPUBindGroup native() const noexcept;
    explicit BindGroup(std::shared_ptr<detail::BindingState> state) : state_(std::move(state)) {}
private:
    std::shared_ptr<detail::BindingState> state_;
    friend class Commands;
};

class Device final {
public:
    Device() = default;
    explicit Device(std::shared_ptr<detail::DeviceState> state) : state_(std::move(state)) {}
    [[nodiscard]] WGPUDevice native() const noexcept;
    [[nodiscard]] WGPUQueue queue() const noexcept;
    [[nodiscard]] WGPULimits limits() const;
    [[nodiscard]] MemoryStats memory() const;
    [[nodiscard]] Status set_memory_budget(std::uint64_t bytes) const;
    [[nodiscard]] bool lost() const;
    [[nodiscard]] std::vector<Diagnostic> take_diagnostics() const;
    void poll() const;
    void destroy() const;
    [[nodiscard]] Future<Buffer> create_buffer(const WGPUBufferDescriptor&) const;
    [[nodiscard]] Future<Texture> create_texture(const WGPUTextureDescriptor&) const;
    [[nodiscard]] Future<ShaderModule> create_shader(std::string_view wgsl, std::string_view label = {}) const;
    [[nodiscard]] Future<Sampler> create_sampler(const WGPUSamplerDescriptor& = WGPU_SAMPLER_DESCRIPTOR_INIT) const;
    [[nodiscard]] Future<BindGroupLayout> create_bind_group_layout(const WGPUBindGroupLayoutDescriptor&) const;
    [[nodiscard]] Future<PipelineLayout> create_pipeline_layout(const WGPUPipelineLayoutDescriptor&) const;
    [[nodiscard]] Future<ComputePipeline> create_compute_pipeline(const WGPUComputePipelineDescriptor&) const;
    [[nodiscard]] Future<RenderPipeline> create_render_pipeline(const WGPURenderPipelineDescriptor&) const;
    [[nodiscard]] Future<QuerySet> create_query_set(const WGPUQuerySetDescriptor&) const;
    [[nodiscard]] Future<BindGroup> create_bind_group(const WGPUBindGroupDescriptor&,
        std::span<const BufferSlice> buffers = {}, std::span<const Texture> textures = {}) const;
    [[nodiscard]] Result<Commands> commands(std::string_view label = {}) const;
    [[nodiscard]] Status write(const BufferSlice&, std::span<const std::byte> data, std::uint64_t offset = 0) const;
    [[nodiscard]] Future<Mapping> map(const BufferSlice&, WGPUMapMode mode) const;
    [[nodiscard]] Future<void> on_submitted_work_done() const;
private:
    std::shared_ptr<detail::DeviceState> state_;
    friend class BufferArena;
};

// An encoder is single-owner and single-submission. retain() must be called
// for managed resources used through native(), including render attachments,
// vertex/index/indirect buffers, and texture copies.
class Commands final {
public:
    Commands();
    Commands(Commands&&) noexcept;
    Commands& operator=(Commands&&) noexcept;
    Commands(const Commands&) = delete;
    Commands& operator=(const Commands&) = delete;
    ~Commands();
    [[nodiscard]] WGPUCommandEncoder native() const noexcept;
    [[nodiscard]] Status retain(const BufferSlice&);
    [[nodiscard]] Status retain(const Texture&);
    [[nodiscard]] Status copy(const BufferSlice& source, const BufferSlice& destination, std::uint64_t bytes);
    [[nodiscard]] Status clear(const BufferSlice&);
    [[nodiscard]] Status dispatch(const ComputePipeline&, std::span<const BindGroup>,
        std::uint32_t x, std::uint32_t y = 1, std::uint32_t z = 1);
    [[nodiscard]] Future<void> submit();
    explicit Commands(std::unique_ptr<detail::CommandState> state);
private:
    std::unique_ptr<detail::CommandState> state_;
};
}
