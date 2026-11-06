#include <iiGPU/DeviceThread.h>
#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace ii::gpu;
using namespace std::chrono_literals;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Gate final {
    std::mutex mutex;std::condition_variable changed;bool entered=false,open=false;
    void wait(){std::unique_lock lock(mutex);entered=true;changed.notify_all();if(!changed.wait_for(lock,5s,[&]{return open;}))throw std::runtime_error("gate timed out");}
    bool await(){std::unique_lock lock(mutex);return changed.wait_for(lock,5s,[&]{return entered;});}
    void release(){std::lock_guard lock(mutex);open=true;changed.notify_all();}
};
struct Release final {Gate& gate;~Release(){gate.release();}};
void bounded_fifo_and_thread_affinity(){
    DeviceThread worker({.capacity=3});Gate gate;Release release{gate};
    const auto caller=std::this_thread::get_id();std::vector<int> order;std::vector<std::thread::id> ids;
    auto first=worker.post([&]{order.push_back(1);ids.push_back(std::this_thread::get_id());gate.wait();});
    const bool entered=gate.await();
    auto second=worker.post([&]{order.push_back(2);ids.push_back(std::this_thread::get_id());});
    auto third=worker.post([&]{order.push_back(3);ids.push_back(std::this_thread::get_id());});
    auto full=worker.post([]{});const auto held=worker.stats();gate.release();
    const auto closed=worker.close();const auto done=worker.stats();
    check(first&&entered&&second&&third&&!full&&full.error().code==ErrorCode::limit_exceeded&&
          held.pending==3&&held.active==1&&held.queued==2,"admission is bounded including active work");
    check(closed&&order==std::vector<int>({1,2,3})&&ids.size()==3&&ids[0]!=caller&&ids[0]==ids[1]&&ids[1]==ids[2],
          "accepted work runs FIFO on one separate, persistent thread");
    check(done.pending==0&&done.active==0&&done.queued==0&&done.executed==3&&done.submitted==3&&
          done.rejected==1&&done.stopped,"drain returns every slot and reports statistics");
    auto rejected=worker.post([]{});check(!rejected&&rejected.error().code==ErrorCode::wrong_state,"closed worker rejects work");
}
void failures_and_self_close(){
    DeviceThread worker({.capacity=4});std::atomic<bool> self_rejected=false,continued=false,current=false;
    auto one=worker.post([&]{current=worker.is_current();auto result=worker.close();self_rejected=!result&&result.error().code==ErrorCode::busy;});
    auto two=worker.post([]{throw std::runtime_error("work failure");});
    auto three=worker.post([&]{continued=true;});
    const auto result=worker.close();const auto stats=worker.stats();
    check(one&&two&&three&&result&&current&&self_rejected&&continued&&stats.failed==1&&worker.failure(),
          "self-close is rejected and exceptions remain observable without killing the worker");
    check(!worker.is_current()&&worker.close(),"close is idempotent and caller is not the worker");
}
void captures_retire_before_close(){
    DeviceThread worker({.capacity=1});Gate run,destruct;Release first{run};Release second{destruct};
    auto capture=std::shared_ptr<int>(new int(1),[&](int* value){destruct.wait();delete value;});
    std::weak_ptr<int> weak=capture;auto accepted=worker.post([capture,&run]{run.wait();});capture.reset();
    const bool entered=run.await();run.release();const bool destroying=destruct.await();
    auto full=worker.post([]{});const auto stats=worker.stats();
    destruct.release();const auto closed=worker.close();
    check(accepted&&entered&&destroying&&!full&&full.error().code==ErrorCode::limit_exceeded&&stats.pending==1&&closed&&weak.expired(),
          "capture destruction completes before the slot is returned and close finishes");
}
void invalid_options(){
    bool rejected=false;try{DeviceThread bad({.capacity=0});}catch(const std::invalid_argument&){rejected=true;}
    DeviceThread worker;auto empty=worker.post({});check(rejected&&!empty&&empty.error().code==ErrorCode::invalid_argument&&worker.close(),"invalid options and empty work are rejected");
}
void stop_drains_accepted_work(){
    DeviceThread worker({.capacity=2});Gate gate;Release release{gate};std::atomic<unsigned> ran=0;
    auto first=worker.post([&]{gate.wait();++ran;});const bool entered=gate.await();
    auto second=worker.post([&]{++ran;});worker.request_stop();worker.request_stop();
    const auto closing=worker.stats();auto rejected=worker.post([]{});gate.release();const auto result=worker.close();
    check(first&&entered&&second&&closing.stopping&&!closing.stopped&&!rejected&&rejected.error().code==ErrorCode::wrong_state&&result&&ran==2&&worker.stats().stopped,
          "stop rejects new work and drains every accepted task");
}
void capture_cannot_join_its_worker(){
    DeviceThread worker;std::atomic<bool> guarded=false;
    struct Capture final {DeviceThread& worker;std::atomic<bool>& guarded;
        ~Capture(){auto result=worker.close();guarded=!result&&result.error().code==ErrorCode::busy;}};
    auto capture=std::shared_ptr<Capture>(new Capture{worker,guarded});
    auto accepted=worker.post([capture]{});capture.reset();
    check(accepted&&worker.close()&&guarded,"a retiring capture cannot join its own worker");
}
void concurrent_producers(){
    constexpr std::size_t producers=8,per_producer=32;
    DeviceThread worker({.capacity=producers*per_producer});
    std::array<std::thread,producers> threads;
    std::array<std::vector<std::size_t>,producers> observed;
    std::atomic<unsigned> rejected=0;
    for(std::size_t i=0;i<producers;++i)threads[i]=std::thread([&,i]{
        for(std::size_t n=0;n<per_producer;++n)if(!worker.post([&,i,n]{observed[i].push_back(n);}))++rejected;
    });
    for(auto& thread:threads)thread.join();
    const auto closed=worker.close();const auto stats=worker.stats();
    check(closed&&rejected==0&&stats.submitted==producers*per_producer&&stats.executed==stats.submitted,
          "concurrent producers lose no accepted work");
    for(const auto& values:observed){
        check(values.size()==per_producer,"each producer's tasks are delivered exactly once");
        for(std::size_t n=0;n<per_producer;++n)check(values[n]==n,"each producer's FIFO order is preserved");
    }
}
}
int main(){try{bounded_fifo_and_thread_affinity();failures_and_self_close();captures_retire_before_close();invalid_options();stop_drains_accepted_work();capture_cannot_join_its_worker();concurrent_producers();}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
