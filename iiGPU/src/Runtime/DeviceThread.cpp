#include "DeviceThread.h"
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace ii::gpu {
namespace {
thread_local const void* current_device_thread=nullptr;
}

struct DeviceThread::Impl final {
    struct State final {
        explicit State(std::size_t capacity):slots(capacity),queue(capacity) {
            free.reserve(capacity);
            for(std::size_t i=capacity;i;--i)free.push_back(i-1);
            stats.capacity=capacity;
        }
        mutable std::mutex mutex;
        std::condition_variable changed;
        std::vector<Work> slots;
        std::vector<std::size_t> free,queue;
        std::size_t head=0,count=0;
        DeviceThreadStats stats;
        std::exception_ptr failure;

        void run() {
            current_device_thread=this;
            for(;;){
                std::size_t index;Work task;
                {
                    std::unique_lock lock(mutex);
                    changed.wait(lock,[&]{return count||stats.stopping;});
                    if(!count){stats.stopped=true;break;}
                    index=queue[head];head=(head+1)%queue.size();--count;
                    slots[index].swap(task);--stats.queued;++stats.active;
                }
                std::exception_ptr error;
                try{task();}catch(...){error=std::current_exception();}
                // Captures can post or request shutdown; never destroy them under the lock.
                task={};
                {
                    std::lock_guard lock(mutex);
                    if(error){++stats.failed;if(!failure)failure=error;}
                    ++stats.executed;--stats.active;--stats.pending;free.push_back(index);
                }
                changed.notify_all();
            }
            current_device_thread=nullptr;
        }
    };
    std::shared_ptr<State> state;
    std::thread thread;
    std::mutex lifecycle;
    bool joined=false;

    explicit Impl(DeviceThreadOptions options){
        if(!options.capacity)throw std::invalid_argument("device thread capacity must be positive");
        state=std::make_shared<State>(options.capacity);
        thread=std::thread([owner=state]{owner->run();});
    }
};

DeviceThread::DeviceThread(DeviceThreadOptions options):impl_(std::make_unique<Impl>(options)){}
DeviceThread::~DeviceThread(){if(!close())std::terminate();}
Result<void> DeviceThread::post(Work task){
    if(!task)return std::unexpected(Error{ErrorCode::invalid_argument,"device thread work is required"});
    const auto state=impl_->state;
    std::unique_lock lock(state->mutex);
    if(state->stats.stopping){++state->stats.rejected;return std::unexpected(Error{ErrorCode::wrong_state,"device thread is stopping"});}
    if(state->free.empty()){++state->stats.rejected;return std::unexpected(Error{ErrorCode::limit_exceeded,"device thread capacity is exhausted"});}
    const auto index=state->free.back();state->free.pop_back();
    state->slots[index].swap(task);state->queue[(state->head+state->count)%state->queue.size()]=index;++state->count;
    ++state->stats.submitted;++state->stats.pending;++state->stats.queued;
    lock.unlock();state->changed.notify_one();return {};
}
void DeviceThread::request_stop(){
    const auto state=impl_->state;
    {std::lock_guard lock(state->mutex);state->stats.stopping=true;}
    state->changed.notify_all();
}
Result<void> DeviceThread::close(){
    if(is_current())return std::unexpected(Error{ErrorCode::busy,"device thread cannot join itself"});
    std::unique_lock lifecycle(impl_->lifecycle,std::try_to_lock);
    if(!lifecycle.owns_lock())return std::unexpected(Error{ErrorCode::busy,"device thread is already closing"});
    request_stop();
    if(!impl_->joined){
        try{if(impl_->thread.joinable())impl_->thread.join();}
        catch(const std::system_error& error){return std::unexpected(Error{ErrorCode::backend_failure,error.what()});}
        impl_->joined=true;
    }
    return {};
}
bool DeviceThread::is_current()const noexcept{return current_device_thread==impl_->state.get();}
DeviceThreadStats DeviceThread::stats()const{std::lock_guard lock(impl_->state->mutex);return impl_->state->stats;}
std::exception_ptr DeviceThread::failure()const{std::lock_guard lock(impl_->state->mutex);return impl_->state->failure;}
} // namespace ii::gpu
