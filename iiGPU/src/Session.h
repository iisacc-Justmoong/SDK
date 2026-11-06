#pragma once

#include "GPU.h"

namespace ii::gpu {

struct SessionOptions final { std::size_t dispatch_capacity=256; };
struct SessionStats final {
    std::size_t capacity=0,active=0;
    std::uint64_t prepared=0,dispatches=0,released=0;
    bool closed=false;
};
class Session;
class DispatchToken final {
public:
    DispatchToken()=default;
private:
    friend class Session;
    std::weak_ptr<void> owner_;
    std::size_t slot_=0;
    std::uint64_t generation_=0;
};

// One device's bounded, reusable immutable dispatch configurations.
// Closing invalidates tokens; commands recorded earlier retain their resources.
class Session final {
public:
    explicit Session(Device,SessionOptions={});
    ~Session();
    Session(const Session&)=delete;
    Session& operator=(const Session&)=delete;
    Session(Session&&)=delete;
    Session& operator=(Session&&)=delete;

    [[nodiscard]] Result<DispatchToken> prepare(const ComputePipeline&,std::span<const Binding>,Extent);
    [[nodiscard]] Result<void> dispatch(CommandList&,const DispatchToken&,std::span<const std::byte> constants={}) const;
    [[nodiscard]] Result<void> release(const DispatchToken&);
    void close();
    [[nodiscard]] SessionStats stats() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
} // namespace ii::gpu
