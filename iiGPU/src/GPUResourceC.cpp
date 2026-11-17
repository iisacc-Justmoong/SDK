#include "GPUResourceC.h"
#include "GPU.h"
#include "ComputeManager.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <span>

using namespace ii::gpu;
struct iigpu_resource_device final { explicit iigpu_resource_device(Device value):device(std::move(value)){} Device device; };
struct iigpu_resource_buffer final { Buffer buffer; };
struct iigpu_resource_manager final { std::unique_ptr<ComputeManager> manager; };
namespace {
int code(ErrorCode error){return 1+static_cast<int>(error);}
template<class F>int protect_resource(F&& fn) noexcept {
    try{return fn();}catch(const std::bad_alloc&){return code(ErrorCode::out_of_memory);}catch(...){return code(ErrorCode::backend_failure);}
}
int invalid(){return code(ErrorCode::invalid_argument);}
}
extern "C" {
int iigpu_resource_manager_create(int backend,uint64_t budget,size_t max_devices,uint32_t chunk,
                                  iigpu_resource_manager** out){return protect_resource([&]{
    if(!out)return invalid();
    *out=nullptr;
    if(backend<0||backend>2||!budget||!max_devices||!chunk)return invalid();
    ComputeManagerOptions options;
    options.memory_budget_per_device=budget;options.max_devices=max_devices;options.max_items_per_chunk=chunk;
    auto manager=ComputeManager::discover(static_cast<Backend>(backend),options);
    if(!manager)return code(manager.error().code);
    *out=new iigpu_resource_manager{std::move(*manager)};return 0;
});}
void iigpu_resource_manager_destroy(iigpu_resource_manager* manager){delete manager;}
int iigpu_resource_manager_count(iigpu_resource_manager* manager,size_t* out){return protect_resource([&]{
    if(!manager||!out)return invalid();*out=manager->manager->adapters().size();return 0;
});}
int iigpu_resource_manager_execute(iigpu_resource_manager* manager,const iigpu_compute_program* source,
        const void* input,size_t input_size,size_t input_stride,void* output,size_t output_size,size_t output_stride){
    return protect_resource([&]{
        if(!manager||!source||!input_stride||!output_stride||(input_size&&!input)||(output_size&&!output)||
           (source->metal_library_size&&!source->metal_library)||(source->spirv_size&&!source->spirv)||
           (source->constants_size&&!source->constants)||source->spirv_size%4||source->constants_size%4||
           source->constants_size>(std::numeric_limits<uint32_t>::max)()-12u||!source->group_size_x||
           (source->metal_entry&&!*source->metal_entry)||(source->vulkan_entry&&!*source->vulkan_entry)||
           input_size%input_stride)return invalid();
        const auto count=input_size/input_stride;
        if(count>(std::numeric_limits<size_t>::max)()/output_stride||count*output_stride!=output_size)return invalid();
        ComputeProgram program;
        if(source->metal_source)program.kernel.metal_source=source->metal_source;
        if(source->metal_entry)program.kernel.metal_entry=source->metal_entry;
        if(source->vulkan_entry)program.kernel.vulkan_entry=source->vulkan_entry;
        program.kernel.metal_library.resize(source->metal_library_size);
        if(source->metal_library_size)std::memcpy(program.kernel.metal_library.data(),source->metal_library,source->metal_library_size);
        program.kernel.spirv.resize(source->spirv_size/4);
        if(source->spirv_size)std::memcpy(program.kernel.spirv.data(),source->spirv,source->spirv_size);
        program.constants.resize(source->constants_size);
        if(source->constants_size)std::memcpy(program.constants.data(),source->constants,source->constants_size);
        program.input_stride=input_stride;program.output_stride=output_stride;
        program.kernel.bindings={Access::read,Access::write};
        program.kernel.constants_bytes=static_cast<uint32_t>(source->constants_size)+12;
        program.kernel.group_size={source->group_size_x,1,1};
        auto result=manager->manager->execute(program,{static_cast<const std::byte*>(input),input_size});
        if(!result)return code(result.error().code);
        if(output_size)std::memcpy(output,result->bytes.data(),output_size);
        return 0;
    });
}
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
