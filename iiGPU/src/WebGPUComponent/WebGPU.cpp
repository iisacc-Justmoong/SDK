#include "WebGPU.h"
#include <algorithm>
#include <atomic>
#include <bit>
#include <functional>
#include <limits>
#include <thread>

namespace ii::webgpu::detail {
struct Diagnostics final {
    std::mutex mutex;
    bool lost = false;
    std::vector<Diagnostic> messages;
};
struct InstanceState final {
    Handle<WGPUInstance> handle;
    std::recursive_mutex events;
    std::mutex diagnostics_mutex;
    // Callback userdata outlives every managed device and native instance callback.
    std::vector<std::shared_ptr<Diagnostics>> diagnostics;
    ~InstanceState() { handle = {}; }
};
struct DeviceState final {
    std::shared_ptr<InstanceState> instance;
    std::shared_ptr<Diagnostics> diagnostics;
    std::recursive_mutex api;
    MemoryBudget budget;
    Handle<WGPUDevice> handle;
    Handle<WGPUQueue> queue;
    WGPULimits limits = WGPU_LIMITS_INIT;
    explicit DeviceState(std::uint64_t limit) : budget(limit) {}
    ~DeviceState() { if (handle) wgpuDeviceDestroy(handle.native()); }
};
struct MapControl final { std::mutex mutex; bool active = false; };
struct BufferState final {
    std::shared_ptr<DeviceState> device;
    Reservation reservation;
    Handle<WGPUBuffer> handle;
    std::shared_ptr<MapControl> map = std::make_shared<MapControl>();
    std::uint64_t size = 0;
    WGPUBufferUsage usage = 0;
};
struct TextureState final {
    std::shared_ptr<DeviceState> device;
    Reservation reservation;
    Handle<WGPUTexture> handle;
};
// Completion cookies retain native references and allocation leases, never a
// DeviceState/InstanceState. Dropping an unobserved future cannot make a cycle.
struct Retained final {
    Handle<WGPUBuffer> buffer;
    Handle<WGPUTexture> texture;
    Reservation reservation;
    RangeAllocation range;
};
struct BindingState final {
    std::shared_ptr<DeviceState> device;
    Handle<WGPUBindGroup> handle;
    std::vector<Retained> retained;
};
struct ViewLifetime final {
    std::shared_ptr<DeviceState> device;
    Retained retained;
};
struct CommandState final {
    std::shared_ptr<DeviceState> device;
    Handle<WGPUCommandEncoder> encoder;
    std::vector<Retained> retained;
    std::vector<Handle<WGPUBindGroup>> groups;
    bool submitted = false;
};
struct MappingState final {
    std::shared_ptr<DeviceState> device;
    Retained retained;
    std::shared_ptr<MapControl> control;
    const std::byte* data = nullptr;
    std::byte* writable = nullptr;
    std::size_t size = 0;
    ~MappingState() {
        wgpuBufferUnmap(retained.buffer.native());
        std::lock_guard lock(control->mutex); control->active = false;
    }
};
void process_events(const std::shared_ptr<InstanceState>& instance) {
    if (!instance) return;
    std::lock_guard lock(instance->events);
    wgpuInstanceProcessEvents(instance->handle.native());
}
Status wait_event(const std::shared_ptr<InstanceState>& instance, WGPUFuture future, std::chrono::nanoseconds timeout) {
#ifdef __EMSCRIPTEN__
    (void)instance; (void)future; (void)timeout;
    return std::unexpected(Error{ErrorCode::Unsupported, "yield to the browser event loop and poll the future"});
#else
    if (!instance) return std::unexpected(Error{ErrorCode::InvalidArgument, "missing WebGPU instance"});
    std::lock_guard lock(instance->events);
    if (future.id) {
        WGPUFutureWaitInfo info = WGPU_FUTURE_WAIT_INFO_INIT;
        info.future = future;
        const auto status = wgpuInstanceWaitAny(instance->handle.native(), 1, &info, static_cast<std::uint64_t>(timeout.count()));
        if (status != WGPUWaitStatus_Success && status != WGPUWaitStatus_TimedOut)
            return std::unexpected(Error{ErrorCode::Internal, "native WebGPU wait failed"});
    }
    wgpuInstanceProcessEvents(instance->handle.native());
    return {};
#endif
}
static std::string message(WGPUStringView text) {
    if (!text.data) return {};
    return text.length == WGPU_STRLEN ? std::string(text.data) : std::string(text.data, text.length);
}
static ErrorCode error_code(WGPUErrorType type) {
    switch (type) {
    case WGPUErrorType_Validation: return ErrorCode::Validation;
    case WGPUErrorType_OutOfMemory: return ErrorCode::OutOfMemory;
    default: return ErrorCode::Internal;
    }
}
static bool is_lost(const std::shared_ptr<DeviceState>& device) {
    if (!device) return true;
    std::lock_guard lock(device->diagnostics->mutex); return device->diagnostics->lost;
}
template<class T> static Future<T> failed(Error error, const std::shared_ptr<InstanceState>& instance = {}) {
    auto state = std::make_shared<AsyncState<T>>();
    complete<T>(state, std::unexpected(std::move(error)));
    return {state, instance};
}
template<class T> struct Checked final {
    std::shared_ptr<AsyncState<T>> state = std::make_shared<AsyncState<T>>();
    std::mutex mutex;
    std::optional<Error> error;
    unsigned remaining = 3;
    std::function<Result<T>()> finish;
    std::vector<Retained> retained;
    void done(std::optional<Error> failure) {
        std::unique_lock lock(mutex);
        if (failure && !error) error = std::move(failure);
        if (--remaining) return;
        auto outcome = error ? Result<T>(std::unexpected(*error)) : finish();
        // Publish completion only after GPU leases have been retired.
        retained.clear();
        finish = {};
        lock.unlock();
        complete<T>(state, std::move(outcome));
    }
};
static void push_scopes(WGPUDevice device) {
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_OutOfMemory);
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Internal);
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
}
template<class T> static void pop_scopes(WGPUDevice device, const std::shared_ptr<Checked<T>>& check) {
    for (unsigned i=0; i<3; ++i) {
        auto* cookie = new std::shared_ptr<Checked<T>>(check);
        WGPUPopErrorScopeCallbackInfo callback = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
        callback.mode = WGPUCallbackMode_AllowProcessEvents;
        callback.userdata1 = cookie;
        callback.callback = [](WGPUPopErrorScopeStatus status, WGPUErrorType type, WGPUStringView msg, void* data, void*) {
            std::unique_ptr<std::shared_ptr<Checked<T>>> own(static_cast<std::shared_ptr<Checked<T>>*>(data));
            if (status != WGPUPopErrorScopeStatus_Success)
                (*own)->done(Error{ErrorCode::Cancelled, message(msg)});
            else if (type != WGPUErrorType_NoError) (*own)->done(Error{error_code(type), message(msg)});
            else (*own)->done({});
        };
        set_future(check->state, wgpuDevicePopErrorScope(device, callback));
    }
}
template<class T, class Create> static Future<T> checked(const std::shared_ptr<DeviceState>& device, Create create) {
    if (!device || is_lost(device)) return failed<T>({ErrorCode::DeviceLost, "device is unavailable"}, device ? device->instance : nullptr);
    auto check = std::make_shared<Checked<T>>();
    std::lock_guard lock(device->api);
    push_scopes(device->handle.native());
    check->finish = create();
    pop_scopes(device->handle.native(), check);
    return {check->state, device->instance, device};
}
template<class Native, class T = Handle<Native>, class Create>
static Future<T> create_handle(const std::shared_ptr<DeviceState>& state, Create create) {
    return checked<T>(state, [&] {
        auto raw = Handle<Native>::adopt(create());
        return [raw=std::move(raw), weak=std::weak_ptr(state)]() mutable -> Result<T> {
            auto device = weak.lock();
            if (!device) return std::unexpected(Error{ErrorCode::Cancelled, "device owner was released"});
            if (!raw) return std::unexpected(Error{ErrorCode::Internal, "WebGPU returned a null object"});
            return T::retain(raw.native(), device);
        };
    });
}
}

namespace ii::webgpu {
using detail::failed;
Result<Context> Context::create() {
    WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
#ifndef __EMSCRIPTEN__
    WGPUInstanceFeatureName feature = WGPUInstanceFeatureName_TimedWaitAny;
    descriptor.requiredFeatureCount = 1;
    descriptor.requiredFeatures = &feature;
#endif
    auto raw = Handle<WGPUInstance>::adopt(wgpuCreateInstance(&descriptor));
    if (!raw) return std::unexpected(Error{ErrorCode::Unavailable, "WebGPU instance creation failed"});
    auto state = std::make_shared<detail::InstanceState>();
    state->handle = std::move(raw);
    return Context(std::move(state));
}
WGPUInstance Context::native() const noexcept { return state_ ? state_->handle.native() : nullptr; }
void Context::poll() const { detail::process_events(state_); }
Future<Adapter> Context::request_adapter(const WGPURequestAdapterOptions& options) const {
    if (!state_) return failed<Adapter>({ErrorCode::InvalidArgument, "empty context"});
    struct Cookie final {
        std::shared_ptr<detail::AsyncState<Adapter>> output;
        std::weak_ptr<detail::InstanceState> instance;
    };
    auto output = std::make_shared<detail::AsyncState<Adapter>>();
    auto* cookie = new Cookie{output, state_};
    WGPURequestAdapterCallbackInfo callback = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.userdata1 = cookie;
    callback.callback = [](WGPURequestAdapterStatus status, WGPUAdapter raw, WGPUStringView msg, void* data, void*) {
        std::unique_ptr<Cookie> own(static_cast<Cookie*>(data));
        auto handle = Handle<WGPUAdapter>::adopt(raw);
        auto instance = own->instance.lock();
        if (status != WGPURequestAdapterStatus_Success || !instance || !raw)
            detail::complete<Adapter>(own->output, std::unexpected(Error{ErrorCode::Unavailable, detail::message(msg)}));
        else detail::complete<Adapter>(own->output, Adapter(Handle<WGPUAdapter>::retain(raw, instance), instance));
    };
    std::lock_guard lock(state_->events);
    detail::set_future(output, wgpuInstanceRequestAdapter(native(), &options, callback));
    return {output, state_};
}
Result<Surface> Context::create_surface(const WGPUSurfaceDescriptor& descriptor) const {
    if (!state_) return std::unexpected(Error{ErrorCode::InvalidArgument, "empty context"});
    auto surface = Surface::adopt(wgpuInstanceCreateSurface(native(), &descriptor), state_);
    if (!surface) return std::unexpected(Error{ErrorCode::Unavailable, "surface creation failed"});
    return surface;
}
AdapterInfo Adapter::info() const {
    if (!handle_) return {};
    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    if (wgpuAdapterGetInfo(native(), &info) != WGPUStatus_Success) return {};
    AdapterInfo result{detail::message(info.vendor), detail::message(info.architecture), detail::message(info.device),
        detail::message(info.description), info.vendorID, info.deviceID, info.backendType, info.adapterType};
    wgpuAdapterInfoFreeMembers(info);
    return result;
}
Result<WGPULimits> Adapter::limits() const {
    WGPULimits limits = WGPU_LIMITS_INIT;
    if (!handle_ || wgpuAdapterGetLimits(native(), &limits) != WGPUStatus_Success)
        return std::unexpected(Error{ErrorCode::Unavailable, "adapter limits unavailable"});
    return limits;
}
std::vector<WGPUFeatureName> Adapter::features() const {
    if (!handle_) return {};
    WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(native(), &features);
    std::vector<WGPUFeatureName> result;
    if (features.featureCount) result.assign(features.features, features.features + features.featureCount);
    wgpuSupportedFeaturesFreeMembers(features);
    return result;
}
Future<Device> Adapter::request_device(const DeviceOptions& options) const {
    if (!handle_) return failed<Device>({ErrorCode::InvalidArgument, "empty adapter"}, instance_);
    struct Cookie final {
        std::shared_ptr<detail::AsyncState<Device>> output;
        std::weak_ptr<detail::InstanceState> instance;
        std::shared_ptr<detail::Diagnostics> diagnostics;
        std::uint64_t budget;
    };
    auto output = std::make_shared<detail::AsyncState<Device>>();
    auto diagnostics = std::make_shared<detail::Diagnostics>();
    {
        std::lock_guard lock(instance_->diagnostics_mutex);
        instance_->diagnostics.push_back(diagnostics);
    }
    auto descriptor = options.descriptor;
    descriptor.deviceLostCallbackInfo = WGPU_DEVICE_LOST_CALLBACK_INFO_INIT;
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    descriptor.deviceLostCallbackInfo.userdata1 = diagnostics.get();
    descriptor.deviceLostCallbackInfo.callback = [](WGPUDevice const*, WGPUDeviceLostReason, WGPUStringView msg, void* data, void*) {
        auto& diag = *static_cast<detail::Diagnostics*>(data);
        std::lock_guard lock(diag.mutex);
        diag.lost = true;
        diag.messages.push_back({ErrorCode::DeviceLost, detail::message(msg)});
    };
    descriptor.uncapturedErrorCallbackInfo = WGPU_UNCAPTURED_ERROR_CALLBACK_INFO_INIT;
    descriptor.uncapturedErrorCallbackInfo.userdata1 = diagnostics.get();
    descriptor.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType type, WGPUStringView msg, void* data, void*) {
        auto& diag = *static_cast<detail::Diagnostics*>(data);
        std::lock_guard lock(diag.mutex);
        diag.messages.push_back({detail::error_code(type), detail::message(msg)});
    };
    WGPURequestDeviceCallbackInfo callback = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.userdata1 = new Cookie{output, instance_, diagnostics, options.memory_budget};
    callback.callback = [](WGPURequestDeviceStatus status, WGPUDevice raw, WGPUStringView msg, void* data, void*) {
        std::unique_ptr<Cookie> own(static_cast<Cookie*>(data));
        auto handle = Handle<WGPUDevice>::adopt(raw);
        auto instance = own->instance.lock();
        if (status != WGPURequestDeviceStatus_Success || !raw || !instance) {
            detail::complete<Device>(own->output, std::unexpected(Error{ErrorCode::Unavailable, detail::message(msg)}));
            return;
        }
        auto device = std::make_shared<detail::DeviceState>(own->budget);
        device->instance = std::move(instance);
        device->diagnostics = own->diagnostics;
        device->handle = std::move(handle);
        device->queue = Handle<WGPUQueue>::adopt(wgpuDeviceGetQueue(raw));
        if (wgpuDeviceGetLimits(raw, &device->limits) != WGPUStatus_Success) {
            detail::complete<Device>(own->output, std::unexpected(Error{ErrorCode::Unavailable, "device limits unavailable"}));
            return;
        }
        detail::complete<Device>(own->output, Device(std::move(device)));
    };
    std::lock_guard lock(instance_->events);
    detail::set_future(output, wgpuAdapterRequestDevice(native(), &descriptor, callback));
    return {output, instance_};
}
WGPUDevice Device::native() const noexcept { return state_ ? state_->handle.native() : nullptr; }
WGPUQueue Device::queue() const noexcept { return state_ ? state_->queue.native() : nullptr; }
WGPULimits Device::limits() const { return state_ ? state_->limits : WGPULimits WGPU_LIMITS_INIT; }
MemoryStats Device::memory() const { return state_ ? state_->budget.stats() : MemoryStats{}; }
Status Device::set_memory_budget(std::uint64_t bytes) const {
    if (!state_) return std::unexpected(Error{ErrorCode::InvalidArgument, "empty device"});
    return state_->budget.set_limit(bytes);
}
bool Device::lost() const { return detail::is_lost(state_); }
std::vector<Diagnostic> Device::take_diagnostics() const {
    if (!state_) return {};
    std::lock_guard lock(state_->diagnostics->mutex);
    return std::exchange(state_->diagnostics->messages, {});
}
void Device::poll() const { if (state_) detail::process_events(state_->instance); }
void Device::destroy() const {
    if (!state_) return;
    std::lock_guard lock(state_->api);
    { std::lock_guard diag(state_->diagnostics->mutex); state_->diagnostics->lost = true; }
    wgpuDeviceDestroy(native());
}
Future<Buffer> Device::create_buffer(const WGPUBufferDescriptor& descriptor) const {
    if (!state_) return failed<Buffer>({ErrorCode::InvalidArgument, "empty device"});
    if (!descriptor.size || descriptor.size > state_->limits.maxBufferSize || !descriptor.usage)
        return failed<Buffer>({ErrorCode::InvalidArgument, "nonzero buffer size/usage within the device limit required"}, state_->instance);
    auto reservation = state_->budget.reserve(descriptor.size);
    if (!reservation) return failed<Buffer>(reservation.error(), state_->instance);
    return detail::checked<Buffer>(state_, [&] {
        auto handle = Handle<WGPUBuffer>::adopt(wgpuDeviceCreateBuffer(native(), &descriptor));
        return [handle=std::move(handle), reservation=std::move(*reservation), weak=std::weak_ptr(state_),
                size=descriptor.size, usage=descriptor.usage]() mutable -> Result<Buffer> {
            auto device = weak.lock();
            if (!device) return std::unexpected(Error{ErrorCode::Cancelled, "device owner was released"});
            if (!handle) return std::unexpected(Error{ErrorCode::OutOfMemory, "buffer allocation returned null"});
            auto buffer = std::make_shared<detail::BufferState>();
            buffer->device = std::move(device); buffer->handle = std::move(handle);
            buffer->reservation = std::move(reservation); buffer->size = size; buffer->usage = usage;
            return Buffer(std::move(buffer));
        };
    });
}
WGPUBuffer Buffer::native() const noexcept { return state_ ? state_->handle.native() : nullptr; }
std::uint64_t Buffer::size() const noexcept { return state_ ? state_->size : 0; }
WGPUBufferUsage Buffer::usage() const noexcept { return state_ ? state_->usage : 0; }
BufferSlice Buffer::whole() const {
    BufferSlice result; result.state_ = state_; result.size_ = size(); return result;
}
Result<BufferSlice> Buffer::slice(std::uint64_t offset, std::uint64_t size) const { return whole().slice(offset, size); }
WGPUBuffer BufferSlice::native() const noexcept { return state_ ? state_->handle.native() : nullptr; }
WGPUBufferUsage BufferSlice::usage() const noexcept { return state_ ? state_->usage : 0; }
Result<BufferSlice> BufferSlice::slice(std::uint64_t offset, std::uint64_t size) const {
    if (!state_ || !size || offset > size_ || size > size_ - offset)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "buffer slice out of bounds"});
    auto result = *this; result.offset_ += offset; result.size_ = size; return result;
}
Future<Texture> Device::create_texture(const WGPUTextureDescriptor& descriptor) const {
    if (!state_) return failed<Texture>({ErrorCode::InvalidArgument, "empty device"});
    auto bytes = texture_bytes(descriptor);
    if (!bytes) return failed<Texture>(bytes.error(), state_->instance);
    auto reservation = state_->budget.reserve(*bytes);
    if (!reservation) return failed<Texture>(reservation.error(), state_->instance);
    return detail::checked<Texture>(state_, [&] {
        auto handle = Handle<WGPUTexture>::adopt(wgpuDeviceCreateTexture(native(), &descriptor));
        return [handle=std::move(handle), reservation=std::move(*reservation), weak=std::weak_ptr(state_)]() mutable -> Result<Texture> {
            auto device = weak.lock();
            if (!device) return std::unexpected(Error{ErrorCode::Cancelled, "device owner was released"});
            if (!handle) return std::unexpected(Error{ErrorCode::OutOfMemory, "texture allocation returned null"});
            auto texture = std::make_shared<detail::TextureState>();
            texture->device = std::move(device); texture->handle = std::move(handle); texture->reservation = std::move(reservation);
            return Texture(std::move(texture));
        };
    });
}
WGPUTexture Texture::native() const noexcept { return state_ ? state_->handle.native() : nullptr; }
std::uint64_t Texture::logical_bytes() const noexcept { return state_ ? state_->reservation.size() : 0; }
Future<TextureView> Texture::view(const WGPUTextureViewDescriptor* descriptor) const {
    if (!state_) return failed<TextureView>({ErrorCode::InvalidArgument, "empty texture"});
    return detail::checked<TextureView>(state_->device, [&] {
        auto handle=TextureView::adopt(wgpuTextureCreateView(native(),descriptor));
        detail::Retained retained{{},Handle<WGPUTexture>::retain(native()),state_->reservation,{}};
        return [handle=std::move(handle), retained=std::move(retained), weak=std::weak_ptr(state_->device)]() mutable -> Result<TextureView> {
            auto device=weak.lock();
            if (!device) return std::unexpected(Error{ErrorCode::Cancelled,"device owner was released"});
            if (!handle) return std::unexpected(Error{ErrorCode::Internal,"texture view creation returned null"});
            auto owner=std::make_shared<detail::ViewLifetime>();
            owner->device=std::move(device); owner->retained=std::move(retained);
            return TextureView::retain(handle.native(),std::move(owner));
        };
    });
}
Future<ShaderModule> Device::create_shader(std::string_view wgsl, std::string_view label) const {
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = string_view(wgsl);
    WGPUShaderModuleDescriptor descriptor = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &source.chain; descriptor.label = string_view(label);
    return detail::create_handle<WGPUShaderModule>(state_, [&] { return wgpuDeviceCreateShaderModule(native(), &descriptor); });
}
Future<Sampler> Device::create_sampler(const WGPUSamplerDescriptor& descriptor) const {
    return detail::create_handle<WGPUSampler>(state_, [&] { return wgpuDeviceCreateSampler(native(), &descriptor); });
}
Future<BindGroupLayout> Device::create_bind_group_layout(const WGPUBindGroupLayoutDescriptor& descriptor) const {
    return detail::create_handle<WGPUBindGroupLayout>(state_, [&] { return wgpuDeviceCreateBindGroupLayout(native(), &descriptor); });
}
Future<PipelineLayout> Device::create_pipeline_layout(const WGPUPipelineLayoutDescriptor& descriptor) const {
    return detail::create_handle<WGPUPipelineLayout>(state_, [&] { return wgpuDeviceCreatePipelineLayout(native(), &descriptor); });
}
Future<ComputePipeline> Device::create_compute_pipeline(const WGPUComputePipelineDescriptor& descriptor) const {
    return detail::create_handle<WGPUComputePipeline>(state_, [&] { return wgpuDeviceCreateComputePipeline(native(), &descriptor); });
}
Future<RenderPipeline> Device::create_render_pipeline(const WGPURenderPipelineDescriptor& descriptor) const {
    return detail::create_handle<WGPURenderPipeline>(state_, [&] { return wgpuDeviceCreateRenderPipeline(native(), &descriptor); });
}
Future<QuerySet> Device::create_query_set(const WGPUQuerySetDescriptor& descriptor) const {
    return detail::create_handle<WGPUQuerySet>(state_, [&] { return wgpuDeviceCreateQuerySet(native(), &descriptor); });
}
Future<BindGroup> Device::create_bind_group(const WGPUBindGroupDescriptor& descriptor,
        std::span<const BufferSlice> buffers, std::span<const Texture> textures) const {
    if (!state_) return failed<BindGroup>({ErrorCode::InvalidArgument, "empty device"});
    if (descriptor.entryCount && !descriptor.entries)
        return failed<BindGroup>({ErrorCode::InvalidArgument, "bind group entries are null"}, state_->instance);
    std::vector<detail::Retained> retained;
    for (const auto& buffer : buffers) {
        if (!buffer.state_ || buffer.state_->device != state_)
            return failed<BindGroup>({ErrorCode::InvalidArgument, "bind group buffer belongs to another device"}, state_->instance);
        retained.push_back({Handle<WGPUBuffer>::retain(buffer.native()), {}, buffer.state_->reservation, buffer.allocation_});
    }
    for (const auto& texture : textures) {
        if (!texture.state_ || texture.state_->device != state_)
            return failed<BindGroup>({ErrorCode::InvalidArgument, "bind group texture belongs to another device"}, state_->instance);
        retained.push_back({{}, Handle<WGPUTexture>::retain(texture.native()), texture.state_->reservation, {}});
    }
    for (std::size_t i=0; i<descriptor.entryCount; ++i) {
        const auto& entry = descriptor.entries[i];
        if (!entry.buffer) continue;
        const auto found = std::find_if(buffers.begin(), buffers.end(), [&](const auto& slice) {
            if (slice.native() != entry.buffer || entry.offset < slice.offset_ || entry.offset - slice.offset_ > slice.size_) return false;
            const auto size = entry.size == WGPU_WHOLE_SIZE ? slice.state_->size - entry.offset : entry.size;
            return size <= slice.size_ - (entry.offset - slice.offset_);
        });
        if (found == buffers.end()) return failed<BindGroup>({ErrorCode::InvalidArgument, "every buffer binding needs a covering managed slice"}, state_->instance);
    }
    return detail::checked<BindGroup>(state_, [&] {
        auto handle = Handle<WGPUBindGroup>::adopt(wgpuDeviceCreateBindGroup(native(), &descriptor));
        return [handle=std::move(handle), retained=std::move(retained), weak=std::weak_ptr(state_)]() mutable -> Result<BindGroup> {
            auto device = weak.lock();
            if (!device) return std::unexpected(Error{ErrorCode::Cancelled, "device owner was released"});
            auto group = std::make_shared<detail::BindingState>();
            group->device = std::move(device); group->handle = std::move(handle); group->retained = std::move(retained);
            return BindGroup(std::move(group));
        };
    });
}
WGPUBindGroup BindGroup::native() const noexcept { return state_ ? state_->handle.native() : nullptr; }
Status Device::write(const BufferSlice& slice, std::span<const std::byte> data, std::uint64_t offset) const {
    if (!state_ || !slice.state_ || slice.state_->device != state_ || !(slice.usage() & WGPUBufferUsage_CopyDst) ||
        offset > slice.size_ || data.size() > slice.size_ - offset || (slice.offset_ + offset) % 4 || data.size() % 4)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "write requires an aligned CopyDst range from this device"});
    std::lock_guard lock(state_->api);
    if (lost()) return std::unexpected(Error{ErrorCode::DeviceLost, "device is lost"});
    if (wgpuBufferGetMapState(slice.native()) != WGPUBufferMapState_Unmapped)
        return std::unexpected(Error{ErrorCode::Validation, "cannot write a mapped or mapping buffer"});
    wgpuQueueWriteBuffer(queue(), slice.native(), slice.offset_ + offset, data.data(), data.size());
    return {};
}
std::span<const std::byte> Mapping::bytes() const noexcept { return state_ ? std::span(state_->data, state_->size) : std::span<const std::byte>{}; }
Result<std::span<std::byte>> Mapping::writable_bytes() const {
    if (!state_ || !state_->writable) return std::unexpected(Error{ErrorCode::InvalidArgument, "mapping is read-only or empty"});
    return std::span(state_->writable, state_->size);
}
Future<Mapping> Device::map(const BufferSlice& slice, WGPUMapMode mode) const {
    if (!state_) return failed<Mapping>({ErrorCode::InvalidArgument, "empty device"});
    const auto usage = mode == WGPUMapMode_Read ? WGPUBufferUsage_MapRead : WGPUBufferUsage_MapWrite;
    if (!slice.state_ || slice.state_->device != state_ || (mode != WGPUMapMode_Read && mode != WGPUMapMode_Write) ||
        !(slice.usage() & usage) || slice.offset_ % 8 || !slice.size_ || slice.size_ % 4 || slice.size_ > std::numeric_limits<std::size_t>::max())
        return failed<Mapping>({ErrorCode::InvalidArgument, "map requires MapRead/MapWrite usage, offset multiple of 8 and size multiple of 4"}, state_->instance);
    if (lost()) return failed<Mapping>({ErrorCode::DeviceLost, "device is lost"}, state_->instance);
    auto control = slice.state_->map;
    {
        std::lock_guard lock(control->mutex);
        if (control->active || wgpuBufferGetMapState(slice.native()) != WGPUBufferMapState_Unmapped)
            return failed<Mapping>({ErrorCode::Validation, "buffer already mapped or mapping"}, state_->instance);
        control->active = true;
    }
    struct Cookie final {
        std::shared_ptr<detail::AsyncState<Mapping>> output;
        std::weak_ptr<detail::DeviceState> device;
        detail::Retained retained;
        std::shared_ptr<detail::MapControl> control;
        std::size_t offset, size;
        WGPUMapMode mode;
    };
    auto output = std::make_shared<detail::AsyncState<Mapping>>();
    auto* cookie = new Cookie{output, state_, {Handle<WGPUBuffer>::retain(slice.native()), {}, slice.state_->reservation, slice.allocation_},
        control, static_cast<std::size_t>(slice.offset_), static_cast<std::size_t>(slice.size_), mode};
    WGPUBufferMapCallbackInfo callback = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents; callback.userdata1 = cookie;
    callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView msg, void* data, void*) {
        std::unique_ptr<Cookie> own(static_cast<Cookie*>(data));
        auto device = own->device.lock();
        if (status != WGPUMapAsyncStatus_Success || !device) {
            if (status == WGPUMapAsyncStatus_Success) wgpuBufferUnmap(own->retained.buffer.native());
            { std::lock_guard lock(own->control->mutex); own->control->active = false; }
            detail::complete<Mapping>(own->output, std::unexpected(Error{device ? ErrorCode::Validation : ErrorCode::Cancelled, detail::message(msg)}));
            return;
        }
        auto mapping = std::make_shared<detail::MappingState>();
        mapping->device = std::move(device); mapping->control = own->control; mapping->size = own->size;
        mapping->retained = std::move(own->retained);
        if (own->mode == WGPUMapMode_Write) {
            mapping->writable = static_cast<std::byte*>(wgpuBufferGetMappedRange(mapping->retained.buffer.native(), own->offset, own->size));
            mapping->data = mapping->writable;
        } else mapping->data = static_cast<const std::byte*>(wgpuBufferGetConstMappedRange(mapping->retained.buffer.native(), own->offset, own->size));
        if (!mapping->data) detail::complete<Mapping>(own->output, std::unexpected(Error{ErrorCode::Validation, "mapped range unavailable"}));
        else detail::complete<Mapping>(own->output, Mapping(std::move(mapping)));
    };
    std::lock_guard lock(state_->api);
    detail::set_future(output, wgpuBufferMapAsync(slice.native(), mode, cookie->offset, cookie->size, callback));
    return {output, state_->instance, state_};
}
Future<void> Device::on_submitted_work_done() const {
    if (!state_ || lost()) return failed<void>({ErrorCode::DeviceLost, "device is unavailable"}, state_ ? state_->instance : nullptr);
    auto output = std::make_shared<detail::AsyncState<void>>();
    WGPUQueueWorkDoneCallbackInfo callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.userdata1 = new std::shared_ptr<detail::AsyncState<void>>(output);
    callback.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView msg, void* data, void*) {
        std::unique_ptr<std::shared_ptr<detail::AsyncState<void>>> own(static_cast<std::shared_ptr<detail::AsyncState<void>>*>(data));
        if (status == WGPUQueueWorkDoneStatus_Success) detail::complete<void>(*own, {});
        else detail::complete<void>(*own, std::unexpected(Error{ErrorCode::DeviceLost, detail::message(msg)}));
    };
    std::lock_guard lock(state_->api);
    detail::set_future(output, wgpuQueueOnSubmittedWorkDone(queue(), callback));
    return {output, state_->instance, state_};
}
Result<Commands> Device::commands(std::string_view label) const {
    if (!state_ || lost()) return std::unexpected(Error{ErrorCode::DeviceLost, "device is unavailable"});
    WGPUCommandEncoderDescriptor descriptor = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    descriptor.label = string_view(label);
    auto commands = std::make_unique<detail::CommandState>();
    commands->device = state_;
    { std::lock_guard lock(state_->api); commands->encoder = Handle<WGPUCommandEncoder>::adopt(wgpuDeviceCreateCommandEncoder(native(), &descriptor)); }
    if (!commands->encoder) return std::unexpected(Error{ErrorCode::Internal, "command encoder creation failed"});
    return Commands(std::move(commands));
}
Commands::Commands() = default;
Commands::Commands(std::unique_ptr<detail::CommandState> state) : state_(std::move(state)) {}
Commands::Commands(Commands&&) noexcept = default;
Commands& Commands::operator=(Commands&&) noexcept = default;
Commands::~Commands() = default;
WGPUCommandEncoder Commands::native() const noexcept { return state_ && !state_->submitted ? state_->encoder.native() : nullptr; }
Status Commands::retain(const BufferSlice& slice) {
    if (!native() || !slice.state_ || slice.state_->device != state_->device)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "encoder/range device mismatch or encoder already submitted"});
    if (wgpuBufferGetMapState(slice.native()) != WGPUBufferMapState_Unmapped)
        return std::unexpected(Error{ErrorCode::Validation, "GPU commands cannot use a mapped or mapping buffer"});
    state_->retained.push_back({Handle<WGPUBuffer>::retain(slice.native()), {}, slice.state_->reservation, slice.allocation_});
    return {};
}
Status Commands::retain(const Texture& texture) {
    if (!native() || !texture.state_ || texture.state_->device != state_->device)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "encoder/texture device mismatch or encoder already submitted"});
    state_->retained.push_back({{}, Handle<WGPUTexture>::retain(texture.native()), texture.state_->reservation, {}});
    return {};
}
Status Commands::copy(const BufferSlice& source, const BufferSlice& destination, std::uint64_t bytes) {
    if (!bytes || bytes % 4 || source.offset_ % 4 || destination.offset_ % 4 || bytes > source.size_ || bytes > destination.size_ ||
        !(source.usage() & WGPUBufferUsage_CopySrc) || !(destination.usage() & WGPUBufferUsage_CopyDst))
        return std::unexpected(Error{ErrorCode::InvalidArgument, "copy requires aligned CopySrc/CopyDst ranges"});
    if (source.native() == destination.native())
        return std::unexpected(Error{ErrorCode::InvalidArgument, "WebGPU buffer copies require different source and destination buffers"});
    if (!native() || !source.state_ || !destination.state_ || source.state_->device != state_->device || destination.state_->device != state_->device)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "copy ranges must belong to the encoder's device"});
    if (auto status = retain(source); !status) return status;
    if (auto status = retain(destination); !status) return status;
    wgpuCommandEncoderCopyBufferToBuffer(native(), source.native(), source.offset_, destination.native(), destination.offset_, bytes);
    return {};
}
Status Commands::clear(const BufferSlice& slice) {
    if (slice.offset_ % 4 || slice.size_ % 4 || !(slice.usage() & WGPUBufferUsage_CopyDst))
        return std::unexpected(Error{ErrorCode::InvalidArgument, "clear requires an aligned CopyDst range"});
    if (auto status = retain(slice); !status) return status;
    wgpuCommandEncoderClearBuffer(native(), slice.native(), slice.offset_, slice.size_);
    return {};
}
Status Commands::dispatch(const ComputePipeline& pipeline, std::span<const BindGroup> groups, std::uint32_t x, std::uint32_t y, std::uint32_t z) {
    if (!native() || !pipeline || !x || !y || !z || x > state_->device->limits.maxComputeWorkgroupsPerDimension ||
        y > state_->device->limits.maxComputeWorkgroupsPerDimension || z > state_->device->limits.maxComputeWorkgroupsPerDimension ||
        groups.size() > state_->device->limits.maxBindGroups)
        return std::unexpected(Error{ErrorCode::InvalidArgument, "invalid pipeline, bind groups or workgroup dimensions"});
    for (const auto& group : groups) {
        if (!group.state_ || group.state_->device != state_->device)
            return std::unexpected(Error{ErrorCode::InvalidArgument, "bind group belongs to another device"});
    }
    for (const auto& group : groups) {
        state_->retained.insert(state_->retained.end(), group.state_->retained.begin(), group.state_->retained.end());
        state_->groups.push_back(Handle<WGPUBindGroup>::retain(group.native()));
    }
    auto pass = Handle<WGPUComputePassEncoder>::adopt(wgpuCommandEncoderBeginComputePass(native(), nullptr));
    wgpuComputePassEncoderSetPipeline(pass.native(), pipeline.native());
    for (std::uint32_t i=0; i<groups.size(); ++i) wgpuComputePassEncoderSetBindGroup(pass.native(), i, groups[i].native(), 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass.native(), x, y, z);
    wgpuComputePassEncoderEnd(pass.native());
    return {};
}
Future<void> Commands::submit() {
    if (!native()) return failed<void>({ErrorCode::InvalidArgument, "encoder has already been submitted or is empty"});
    auto device = state_->device;
    if (detail::is_lost(device)) return failed<void>({ErrorCode::DeviceLost, "device is lost"}, device->instance);
    auto check = std::make_shared<detail::Checked<void>>();
    check->remaining = 4;
    check->finish = []() -> Status { return {}; };
    check->retained = std::move(state_->retained);
    std::lock_guard lock(device->api);
    detail::push_scopes(device->handle.native());
    state_->submitted = true;
    auto command = Handle<WGPUCommandBuffer>::adopt(wgpuCommandEncoderFinish(state_->encoder.native(), nullptr));
    WGPUCommandBuffer raw = command.native();
    wgpuQueueSubmit(device->queue.native(), 1, &raw);
    detail::pop_scopes(device->handle.native(), check);
    WGPUQueueWorkDoneCallbackInfo callback = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    callback.mode = WGPUCallbackMode_AllowProcessEvents;
    callback.userdata1 = new std::shared_ptr<detail::Checked<void>>(check);
    callback.callback = [](WGPUQueueWorkDoneStatus status, WGPUStringView msg, void* data, void*) {
        std::unique_ptr<std::shared_ptr<detail::Checked<void>>> own(static_cast<std::shared_ptr<detail::Checked<void>>*>(data));
        if (status == WGPUQueueWorkDoneStatus_Success) (*own)->done({});
        else (*own)->done(Error{ErrorCode::DeviceLost, detail::message(msg)});
    };
    detail::set_future(check->state, wgpuQueueOnSubmittedWorkDone(device->queue.native(), callback));
    state_->encoder = {}; state_->groups.clear();
    return {check->state, device->instance, device};
}
}
