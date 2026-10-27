#pragma once

#include <webgpu/webgpu.h>
#include <memory>
#include <string_view>
#include <utility>

namespace ii::webgpu {
inline WGPUStringView string_view(std::string_view text) noexcept { return {text.data(), text.size()}; }
namespace detail {
template<class T> struct NativeTraits;
#define IIWEBGPU_NATIVE_TRAITS(Name) \
    template<> struct NativeTraits<WGPU##Name> final { \
        static void add_ref(WGPU##Name h) noexcept { wgpu##Name##AddRef(h); } \
        static void release(WGPU##Name h) noexcept { wgpu##Name##Release(h); } \
    };
IIWEBGPU_NATIVE_TRAITS(Instance)
IIWEBGPU_NATIVE_TRAITS(Adapter)
IIWEBGPU_NATIVE_TRAITS(Device)
IIWEBGPU_NATIVE_TRAITS(Queue)
IIWEBGPU_NATIVE_TRAITS(Buffer)
IIWEBGPU_NATIVE_TRAITS(Texture)
IIWEBGPU_NATIVE_TRAITS(TextureView)
IIWEBGPU_NATIVE_TRAITS(Sampler)
IIWEBGPU_NATIVE_TRAITS(ShaderModule)
IIWEBGPU_NATIVE_TRAITS(BindGroupLayout)
IIWEBGPU_NATIVE_TRAITS(BindGroup)
IIWEBGPU_NATIVE_TRAITS(PipelineLayout)
IIWEBGPU_NATIVE_TRAITS(ComputePipeline)
IIWEBGPU_NATIVE_TRAITS(RenderPipeline)
IIWEBGPU_NATIVE_TRAITS(CommandEncoder)
IIWEBGPU_NATIVE_TRAITS(CommandBuffer)
IIWEBGPU_NATIVE_TRAITS(ComputePassEncoder)
IIWEBGPU_NATIVE_TRAITS(RenderPassEncoder)
IIWEBGPU_NATIVE_TRAITS(RenderBundleEncoder)
IIWEBGPU_NATIVE_TRAITS(RenderBundle)
IIWEBGPU_NATIVE_TRAITS(QuerySet)
IIWEBGPU_NATIVE_TRAITS(Surface)
#undef IIWEBGPU_NATIVE_TRAITS
}
// Adopt transfers one native reference. Retain borrows and adds a reference.
// The optional owner keeps managed device/retirement state alive with a handle.
template<class T> class Handle final {
public:
    Handle() = default;
    static Handle adopt(T raw, std::shared_ptr<void> owner = {}) noexcept {
        Handle h; h.raw_ = raw; h.owner_ = std::move(owner); return h;
    }
    static Handle retain(T raw, std::shared_ptr<void> owner = {}) noexcept {
        if (raw) detail::NativeTraits<T>::add_ref(raw);
        return adopt(raw, std::move(owner));
    }
    Handle(const Handle& h) noexcept : Handle(retain(h.raw_, h.owner_)) {}
    Handle(Handle&& h) noexcept : raw_(std::exchange(h.raw_, nullptr)), owner_(std::move(h.owner_)) {}
    Handle& operator=(Handle h) noexcept { swap(h); return *this; }
    ~Handle() { if (raw_) detail::NativeTraits<T>::release(raw_); }
    void swap(Handle& h) noexcept { std::swap(raw_, h.raw_); owner_.swap(h.owner_); }
    [[nodiscard]] T native() const noexcept { return raw_; }
    explicit operator bool() const noexcept { return raw_ != nullptr; }
private:
    T raw_ = nullptr;
    std::shared_ptr<void> owner_;
};
using ShaderModule = Handle<WGPUShaderModule>;
using Sampler = Handle<WGPUSampler>;
using TextureView = Handle<WGPUTextureView>;
using BindGroupLayout = Handle<WGPUBindGroupLayout>;
using PipelineLayout = Handle<WGPUPipelineLayout>;
using ComputePipeline = Handle<WGPUComputePipeline>;
using RenderPipeline = Handle<WGPURenderPipeline>;
using QuerySet = Handle<WGPUQuerySet>;
using Surface = Handle<WGPUSurface>;
}
