#include "../../src/WebGPUComponent/BindingIndex.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using ii::webgpu::detail::BindingIndex;
struct Slice {const void* buffer;std::uint64_t offset,size,total;};
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
bool reference(const std::vector<Slice>& slices,const void* buffer,std::uint64_t offset,std::uint64_t size,bool whole){
    for(const auto& slice:slices){
        if(slice.buffer!=buffer||offset<slice.offset||offset-slice.offset>slice.size)continue;
        const auto wanted=whole?slice.total-offset:size;
        if(wanted<=slice.size-(offset-slice.offset))return true;
    }
    return false;
}
void boundaries(){
    std::array<int,2> buffers{};BindingIndex index;
    index.add(&buffers[0],0,4,10);index.add(&buffers[0],2,4,10);index.add(&buffers[0],4,6,10);
    index.add(&buffers[1],0,2,2);index.finish();
    check(!index.covers(&buffers[0],0,7,false),"multiple overlapping leases cannot jointly cover one binding");
    check(index.covers(&buffers[0],4,0,true)&&!index.covers(&buffers[0],3,0,true),"whole-buffer range used the wrong lease");
    check(index.covers(&buffers[1],2,0,false)&&!index.covers(&buffers[1],3,0,false),"empty boundary is incorrect");
    check(!index.covers(&buffers[1],1,(std::numeric_limits<std::uint64_t>::max)(),false),"overflow was admitted");
    int large=0;BindingIndex high;const auto max=(std::numeric_limits<std::uint64_t>::max)();
    high.add(&large,max-10,10,max);high.finish();
    check(high.covers(&large,max-2,2,false)&&high.covers(&large,max-2,0,true)&&
          !high.covers(&large,max-2,3,false),"upper-bound range arithmetic overflowed");
    BindingIndex single;single.add(&buffers[0],4,6,10);single.finish();
    check(single.covers(&buffers[0],5,5,false)&&single.covers(&buffers[0],5,0,true)&&
          !single.covers(&buffers[0],3,0,true)&&!single.covers(&buffers[1],5,0,true),"single-slice coverage is incorrect");
}
void compare_to_reference(){
    std::array<int,8> buffers{};std::vector<Slice> slices;BindingIndex index;std::mt19937_64 random(0x125d7);
    for(unsigned i=0;i<200;++i){const auto which=random()%buffers.size(),offset=random()%65,size=random()%(65-offset);
        slices.push_back({&buffers[which],offset,size,64});index.add(&buffers[which],offset,size,64);}
    index.finish();
    for(unsigned i=0;i<10000;++i){const auto which=random()%buffers.size(),offset=random()%80,size=random()%80;const bool whole=random()%2;
        check(index.covers(&buffers[which],offset,size,whole)==reference(slices,&buffers[which],offset,size,whole),"indexed coverage differs from individual managed slices");}
    int unknown=0;check(!index.covers(&unknown,0,0,false),"unknown buffer gained a lease");
}
}
int main(){try{boundaries();compare_to_reference();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
