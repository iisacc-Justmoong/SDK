#include <iiSDKCore/Runtime/EventLoop.h>
#include <iiSDKCore/Core/Text/Utf8.h>
#include <iiColorManager/ColorManager.h>
#include <chrono>
#include <atomic>
#include <iostream>
#include <string>
#include <stdexcept>
using Clock=std::chrono::steady_clock;
template<class F>void measure(const char* name,F&& f){auto begin=Clock::now();auto result=f();auto ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();std::cout<<name<<" "<<ms<<" "<<result<<std::endl;}
int main(){
 for(auto capacity:{1024U,65536U}){
  ii::runtime::EventLoop loop(capacity);std::size_t calls=0;auto name=std::string("sparse_dispatch_")+std::to_string(capacity);
  measure(name.c_str(),[&]{for(int n=0;n<3000;++n){if(!loop.post([&]{++calls;}))throw std::runtime_error("post");if(loop.poll({1,std::chrono::seconds(1)})!=1)throw std::runtime_error("poll");}return calls;});
  name="idle_deadline_"+std::to_string(capacity);measure(name.c_str(),[&]{unsigned total=0;for(int n=0;n<10000;++n)total+=loop.next_deadline().has_value();return total;});
 }
 std::string text(1024*1024,'a');measure("utf8_ascii_100MiB",[&]{unsigned total=0;for(int i=0;i<100;++i){std::atomic_signal_fence(std::memory_order_seq_cst);total+=ii::core::valid_utf8(text);}return total;});
 std::string mixed;while(mixed.size()<1024*1024)mixed+="한글🎵hello world ";measure("utf8_mixed_100MiB",[&]{unsigned total=0;for(int i=0;i<100;++i){std::atomic_signal_fence(std::memory_order_seq_cst);total+=ii::core::valid_utf8(mixed);}return total;});
 iiColorManager::Picker picker;if(!picker.begin({.23,.41,.57,.3}))throw std::runtime_error("picker");
 for(auto surface:{iiColorManager::Surface::HueRail,iiColorManager::Surface::BrightnessRail,iiColorManager::Surface::ChannelRamp}){auto name="picker_surface_"+std::to_string(int(surface));measure(name.c_str(),[&]{auto image=iiColorManager::renderSurface(picker,surface,1024,1024,iiColorManager::Channel::Red);if(!image)throw std::runtime_error("surface");unsigned checksum=0;for(auto byte:image->rgba)checksum+=byte;return checksum;});}
}
