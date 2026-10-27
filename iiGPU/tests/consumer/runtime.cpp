#include <iiGPU/Completion.h>
#include <chrono>
#include <thread>

int main() {
    auto d=ii::gpu::Device::create(); if(!d) return 1;
    auto b=d->buffer({256}); if(!b) return 2;
    auto c=d->commands(); if(!c.fill(*b,0)||!c.finish()) return 3;
    auto job=d->submit(std::move(c)); if(!job) return 4;
    ii::runtime::EventLoop loop(4); unsigned calls=0;
    auto watch=ii::gpu::CompletionWatch::create(loop,*job,[&](ii::gpu::Result<void> result){if(result)++calls;});
    if(!watch) return 5;
    auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(watch->active()&&std::chrono::steady_clock::now()<end) {loop.poll();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    return calls==1?0:6;
}
