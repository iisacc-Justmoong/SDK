#pragma once
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stop_token>

namespace iiGlobalSearch::detail {
enum class WebWake { signaled, cancelled, deadline };
class WebWait final {
public:
    void signal() noexcept {
        { std::lock_guard lock(mutex_); signaled_=true; }
        changed_.notify_one();
    }
    WebWake wait(std::stop_token stop, std::chrono::steady_clock::time_point deadline) {
        std::unique_lock lock(mutex_);
        const auto ready=[&] { return signaled_; };
        const bool signaled=deadline==std::chrono::steady_clock::time_point::max()
            ? changed_.wait(lock,stop,ready) : changed_.wait_until(lock,stop,deadline,ready);
        if(stop.stop_requested()) { signaled_=false; return WebWake::cancelled; }
        if(!signaled) return WebWake::deadline;
        signaled_=false; return WebWake::signaled;
    }
    template<class Loop>
    WebWake wait_for_loop(Loop& loop, std::stop_token stop, std::chrono::steady_clock::time_point deadline) {
        if(const auto next=loop.next_deadline(); next && *next<deadline) deadline=*next;
        return wait(stop,deadline);
    }
private:
    std::mutex mutex_;
    std::condition_variable_any changed_;
    bool signaled_=false;
};
}
