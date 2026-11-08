#include "GPUResourceC.h"
#include "GPU.h"
#include <algorithm>
#include <array>
#include <memory>
#include <new>
#include <span>

using namespace ii::gpu;
struct iigpu_resource_device final { explicit iigpu_resource_device(Device value):device(std::move(value)){} Device device; };
struct iigpu_resource_buffer final { Buffer buffer; };
namespace {
int code(ErrorCode error){return 1+static_cast<int>(error);}
template<class F>int protect_resource(F&& fn) noexcept {
    try{return fn();}catch(const std::bad_alloc&){return code(ErrorCode::out_of_memory);}catch(...){return code(ErrorCode::backend_failure);}
}
int invalid(){return code(ErrorCode::invalid_argument);}
}
extern "C" {
int iigpu_resource_device_create(int backend,uint64_t budget,iigpu_resource_device** out){return protect_resource([&]{
    if(!out||backend<0||backend>2||!budget)return invalid();*out=nullptr;
    auto device=Device::create({.backend=static_cast<Backend>(backend),.memory_budget=budget});
    if(!device)return code(device.error().code);*out=new iigpu_resource_device(std::move(*device));return 0;
});}
void iigpu_resource_device_destroy(iigpu_resource_device* device){delete device;}
int iigpu_resource_device_budget(iigpu_resource_device* device,uint64_t* out){return protect_resource([&]{
    if(!device||!out)return invalid();auto value=device->device.memory_budget();if(!value)return code(value.error().code);*out=*value;return 0;
});}
int iigpu_resource_device_set_budget(iigpu_resource_device* device,uint64_t budget){return protect_resource([&]{
    if(!device)return invalid();auto value=device->device.set_memory_budget(budget);return value?0:code(value.error().code);
});}
int iigpu_resource_device_allocated(iigpu_resource_device* device,uint64_t* out){return protect_resource([&]{
    if(!device||!out)return invalid();*out=device->device.stats().allocated_bytes;return 0;
});}
int iigpu_resource_buffer_create(iigpu_resource_device* device,uint64_t size,int device_memory,iigpu_resource_buffer** out){return protect_resource([&]{
    if(!device||!out||(device_memory!=0&&device_memory!=1)||!size||(device_memory&&size%4))return invalid();*out=nullptr;
    auto native=device->device.buffer({.bytes=size,.memory=device_memory?Memory::device:Memory::shared});if(!native)return code(native.error().code);
    if(device_memory){
        auto commands=device->device.commands();auto filled=commands.fill(*native,0);if(!filled)return code(filled.error().code);
        auto finished=commands.finish();if(!finished)return code(finished.error().code);
        auto submitted=device->device.submit(std::move(commands));if(!submitted)return code(submitted.error().code);
        auto waited=submitted->wait();if(!waited)return code(waited.error().code);
    }else{
        std::array<std::byte,65536> zeros{};
        for(uint64_t offset=0;offset<size;){const auto n=static_cast<size_t>(std::min<uint64_t>(zeros.size(),size-offset));
            auto written=native->upload(std::span<const std::byte>(zeros.data(),n),offset);if(!written)return code(written.error().code);offset+=n;}
    }
    auto result=std::make_unique<iigpu_resource_buffer>();result->buffer=std::move(*native);*out=result.release();return 0;
});}
void iigpu_resource_buffer_destroy(iigpu_resource_buffer* buffer){delete buffer;}
int iigpu_resource_buffer_size(iigpu_resource_buffer* buffer,uint64_t* out){return protect_resource([&]{
    if(!buffer||!out||!buffer->buffer)return invalid();*out=buffer->buffer.size();return 0;
});}
int iigpu_resource_buffer_read(iigpu_resource_buffer* buffer,uint64_t offset,void* data,size_t length){return protect_resource([&]{
    if(!buffer||(!data&&length))return invalid();auto value=buffer->buffer.read({static_cast<std::byte*>(data),length},offset);return value?0:code(value.error().code);
});}
int iigpu_resource_buffer_write(iigpu_resource_buffer* buffer,uint64_t offset,const void* data,size_t length){return protect_resource([&]{
    if(!buffer||(!data&&length))return invalid();auto value=buffer->buffer.upload({static_cast<const std::byte*>(data),length},offset);return value?0:code(value.error().code);
});}
int iigpu_resource_buffer_transfer(iigpu_resource_device* destination,iigpu_resource_buffer* source,int move,int device_memory,uint64_t chunk,iigpu_resource_buffer** out){return protect_resource([&]{
    if(!destination||!source||!out||(move!=0&&move!=1)||(device_memory!=0&&device_memory!=1))return invalid();*out=nullptr;
    auto holder=std::make_unique<iigpu_resource_buffer>();BufferTransferOptions options{.memory=device_memory?Memory::device:Memory::shared,.chunk_bytes=chunk};
    auto transferred=move?destination->device.move_buffer(source->buffer,options):destination->device.import_buffer(source->buffer,options);
    if(!transferred)return code(transferred.error().code);holder->buffer=std::move(*transferred);*out=holder.release();return 0;
});}
}
