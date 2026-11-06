#pragma once

#include "GPU.h"
#include <exception>

namespace ii::gpu {

struct DeviceThreadOptions final { std::size_t capacity=64; };
struct DeviceThreadStats final {
    std::size_t capacity=0,pending=0,queued=0,active=0;
    std::uint64_t submitted=0,executed=0,failed=0,rejected=0;
    bool stopping=false,stopped=false;
};

// A single, persistent submission thread with bounded FIFO admission. The
// caller owns it; request_stop closes admission and close drains accepted work.
// GPU execution completion is independent of the end of a submitted callback.
class DeviceThread final {
public:
    using Work=std::function<void()>;
    explicit DeviceThread(DeviceThreadOptions={});
    ~DeviceThread();
    DeviceThread(const DeviceThread&)=delete;
    DeviceThread& operator=(const DeviceThread&)=delete;
    DeviceThread(DeviceThread&&)=delete;
    DeviceThread& operator=(DeviceThread&&)=delete;

    [[nodiscard]] Result<void> post(Work);
    void request_stop();
    [[nodiscard]] Result<void> close();
    [[nodiscard]] bool is_current() const noexcept;
    [[nodiscard]] DeviceThreadStats stats() const;
    [[nodiscard]] std::exception_ptr failure() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace ii::gpu
