#pragma once
#include "GPU.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <variant>

namespace ii::gpu::detail {
class Failure final : public std::exception {
public:
    Error error;
    Failure(ErrorCode code, std::string message):error{code,std::move(message)} {}
    const char* what() const noexcept override { return error.message.c_str(); }
};
[[noreturn]] inline void fail(ErrorCode code, std::string message) { throw Failure(code,std::move(message)); }
inline void require(bool yes, ErrorCode code, const char* message) { if(!yes) fail(code,message); }
template<class F> auto protect(F&& f) -> Result<decltype(f())> {
    try { return f(); }
    catch(const Failure& e) { return std::unexpected(e.error); }
    catch(const std::bad_alloc&) { return std::unexpected(Error{ErrorCode::out_of_memory,"host allocation failed"}); }
    catch(const std::exception& e) { return std::unexpected(Error{ErrorCode::backend_failure,e.what()}); }
}
template<class F> Result<void> protect_void(F&& f) {
    try { f(); return {}; }
    catch(const Failure& e) { return std::unexpected(e.error); }
    catch(const std::bad_alloc&) { return std::unexpected(Error{ErrorCode::out_of_memory,"host allocation failed"}); }
    catch(const std::exception& e) { return std::unexpected(Error{ErrorCode::backend_failure,e.what()}); }
}
struct NativeBuffer {
    virtual ~NativeBuffer()=default;
    virtual void upload(std::span<const std::byte>,std::uint64_t)=0;
    virtual void read(std::span<std::byte>,std::uint64_t)=0;
    virtual std::uint64_t handle() const=0;
    virtual std::uint64_t allocation_bytes() const=0;
};
struct NativePipeline { virtual ~NativePipeline()=default; };
struct NativeSubmission {
    virtual ~NativeSubmission()=default;
    // False means timeout/not ready. Errors must report device loss.
    virtual bool wait(std::chrono::nanoseconds)=0;
};
struct BufferState final {
    std::shared_ptr<DeviceState> owner;
    BufferDesc desc;
    std::unique_ptr<NativeBuffer> native;
    std::size_t pending=0;
    std::uint64_t charged=0;
    ~BufferState();
};
struct PipelineState final {
    std::shared_ptr<DeviceState> owner;
    ComputeDesc desc;
    std::unique_ptr<NativePipeline> native;
};
struct BoundBuffer final { std::shared_ptr<BufferState> buffer; std::uint64_t offset,bytes; };
struct Copy final { std::shared_ptr<BufferState> source,destination; std::uint64_t bytes,source_offset,destination_offset; };
struct Fill final { std::shared_ptr<BufferState> buffer; std::uint32_t pattern; std::uint64_t offset,bytes; };
struct Dispatch final {
    std::shared_ptr<PipelineState> pipeline;
    std::vector<BoundBuffer> bindings;
    Extent groups;
    std::vector<std::byte> constants;
};
struct Barrier final {};
using Command=std::variant<Copy,Fill,Dispatch,Barrier>;
struct CommandsState final {
    std::shared_ptr<DeviceState> owner;
    std::vector<Command> commands;
    enum class Phase { recording, executable, submitted } phase=Phase::recording;
};
struct NativeDevice {
    Adapter adapter;
    Limits limits;
    virtual ~NativeDevice()=default;
    virtual std::unique_ptr<NativeBuffer> buffer(BufferDesc)=0;
    virtual std::unique_ptr<NativePipeline> pipeline(const ComputeDesc&)=0;
    virtual std::unique_ptr<NativeSubmission> submit(const std::vector<Command>&)=0;
    virtual NativeContext context() const=0;
};
struct DeviceState final {
    Options options;
    std::unique_ptr<NativeDevice> native;
    mutable std::mutex mutex;
    std::atomic<std::uint64_t> allocated{0};
    Stats stats;
    std::vector<std::weak_ptr<SubmissionState>> jobs;
};
struct SubmissionState final {
    std::shared_ptr<DeviceState> owner;
    std::weak_ptr<SubmissionState> self;
    std::unique_ptr<NativeSubmission> native;
    std::vector<std::shared_ptr<BufferState>> resources;
    std::vector<std::shared_ptr<PipelineState>> pipelines;
    mutable std::mutex mutex;
    bool retired=false;
    std::optional<Error> error;
    ~SubmissionState();
    Result<bool> wait(std::chrono::nanoseconds);
    void retire();
};
std::vector<Adapter> metal_adapters();
std::unique_ptr<NativeDevice> metal_device(const Options&);
std::vector<Adapter> vulkan_adapters();
std::unique_ptr<NativeDevice> vulkan_device(const Options&);
} // namespace ii::gpu::detail
