#include "Backend.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <type_traits>

namespace ii::gpu::detail {
namespace {
void checked(VkResult result,const char* operation) {
    if(result==VK_SUCCESS) return;
    const auto code=(result==VK_ERROR_OUT_OF_DEVICE_MEMORY||result==VK_ERROR_OUT_OF_HOST_MEMORY)?ErrorCode::out_of_memory:ErrorCode::backend_failure;
    fail(code,std::string(operation)+": VkResult="+std::to_string(result));
}
template<class T> std::uint64_t bits(T value) {
    if constexpr(std::is_pointer_v<T>) return reinterpret_cast<std::uint64_t>(value);
    else return static_cast<std::uint64_t>(value);
}
struct Instance final {
    VkInstance handle=VK_NULL_HANDLE;
    Instance() {
        std::uint32_t count=0;
        checked(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr),"enumerate Vulkan instance extensions");
        std::vector<VkExtensionProperties> available(count);
        checked(vkEnumerateInstanceExtensionProperties(nullptr,&count,available.data()),"enumerate Vulkan instance extensions");
        std::vector<const char*> extensions;
        VkInstanceCreateFlags flags=0;
        for(const auto& e:available) if(std::strcmp(e.extensionName,"VK_KHR_portability_enumeration")==0) {
            extensions.push_back("VK_KHR_portability_enumeration"); flags|=0x00000001u;
        }
        VkApplicationInfo app{}; app.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app.pApplicationName="iiGPU"; app.apiVersion=VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        info.flags=flags; info.pApplicationInfo=&app;
        info.enabledExtensionCount=static_cast<std::uint32_t>(extensions.size()); info.ppEnabledExtensionNames=extensions.data();
        checked(vkCreateInstance(&info,nullptr,&handle),"create Vulkan instance");
    }
    ~Instance() { if(handle) vkDestroyInstance(handle,nullptr); }
};
std::vector<VkPhysicalDevice> physical_devices(const Instance& instance) {
    std::uint32_t count=0; checked(vkEnumeratePhysicalDevices(instance.handle,&count,nullptr),"enumerate Vulkan adapters");
    std::vector<VkPhysicalDevice> result(count);
    checked(vkEnumeratePhysicalDevices(instance.handle,&count,result.data()),"enumerate Vulkan adapters");
    return result;
}
Adapter describe(VkPhysicalDevice physical,std::size_t index) {
    VkPhysicalDeviceProperties p{}; vkGetPhysicalDeviceProperties(physical,&p);
    DeviceKind kind=DeviceKind::unknown;
    switch(p.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:kind=DeviceKind::integrated;break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:kind=DeviceKind::discrete;break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:kind=DeviceKind::virtual_gpu;break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:kind=DeviceKind::cpu;break;
        default:break;
    }
    return {Backend::vulkan,index,p.deviceName,kind,kind==DeviceKind::integrated||kind==DeviceKind::discrete};
}
struct VulkanOwner final {
    std::shared_ptr<Instance> instance;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkPhysicalDeviceProperties properties{};
    VkPhysicalDeviceMemoryProperties memory{};
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    std::uint32_t family=0;
    ~VulkanOwner() {
        if(device) { (void)vkDeviceWaitIdle(device); vkDestroyDevice(device,nullptr); }
    }
};
struct VulkanBuffer final : NativeBuffer {
    std::shared_ptr<VulkanOwner> owner;
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    void* mapped=nullptr;
    std::uint64_t allocated=0;
    bool coherent=false;
    explicit VulkanBuffer(std::shared_ptr<VulkanOwner> value):owner(std::move(value)) {}
    ~VulkanBuffer() {
        if(mapped) vkUnmapMemory(owner->device,memory);
        if(buffer) vkDestroyBuffer(owner->device,buffer,nullptr);
        if(memory) vkFreeMemory(owner->device,memory,nullptr);
    }
    void upload(std::span<const std::byte> bytes,std::uint64_t offset) override {
        if(bytes.empty()) return;
        std::memcpy(static_cast<std::byte*>(mapped)+offset,bytes.data(),bytes.size());
        if(!coherent) {
            VkMappedMemoryRange range{}; range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory=memory; range.size=VK_WHOLE_SIZE;
            checked(vkFlushMappedMemoryRanges(owner->device,1,&range),"flush Vulkan host writes");
        }
    }
    void read(std::span<std::byte> bytes,std::uint64_t offset) override {
        if(bytes.empty()) return;
        if(!coherent) {
            VkMappedMemoryRange range{}; range.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory=memory; range.size=VK_WHOLE_SIZE;
            checked(vkInvalidateMappedMemoryRanges(owner->device,1,&range),"invalidate Vulkan host reads");
        }
        std::memcpy(bytes.data(),static_cast<const std::byte*>(mapped)+offset,bytes.size());
    }
    std::uint64_t handle() const override { return bits(buffer); }
    std::uint64_t allocation_bytes() const override { return allocated; }
};
struct VulkanPipeline final : NativePipeline {
    std::shared_ptr<VulkanOwner> owner;
    VkDescriptorSetLayout descriptors=VK_NULL_HANDLE;
    VkPipelineLayout layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkShaderModule shader=VK_NULL_HANDLE;
    explicit VulkanPipeline(std::shared_ptr<VulkanOwner> value):owner(std::move(value)) {}
    ~VulkanPipeline() {
        if(pipeline) vkDestroyPipeline(owner->device,pipeline,nullptr);
        if(layout) vkDestroyPipelineLayout(owner->device,layout,nullptr);
        if(descriptors) vkDestroyDescriptorSetLayout(owner->device,descriptors,nullptr);
        if(shader) vkDestroyShaderModule(owner->device,shader,nullptr);
    }
};
struct VulkanSubmission final : NativeSubmission {
    std::shared_ptr<VulkanOwner> owner;
    VkCommandPool pool=VK_NULL_HANDLE;
    VkDescriptorPool descriptors=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    explicit VulkanSubmission(std::shared_ptr<VulkanOwner> value):owner(std::move(value)) {}
    ~VulkanSubmission() {
        if(fence) vkDestroyFence(owner->device,fence,nullptr);
        if(descriptors) vkDestroyDescriptorPool(owner->device,descriptors,nullptr);
        if(pool) vkDestroyCommandPool(owner->device,pool,nullptr);
    }
    bool wait(std::chrono::nanoseconds timeout) override {
        const auto value=timeout==std::chrono::nanoseconds::max()?UINT64_MAX:static_cast<std::uint64_t>(timeout.count());
        const auto result=vkWaitForFences(owner->device,1,&fence,VK_TRUE,value);
        if(result==VK_TIMEOUT) return false;
        checked(result,"wait Vulkan fence"); return true;
    }
};
void memory_barrier(VkCommandBuffer command,bool initial=false,bool final=false) {
    VkMemoryBarrier barrier{}; barrier.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    if(initial) barrier.srcAccessMask|=VK_ACCESS_HOST_WRITE_BIT;
    if(final) barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT|VK_ACCESS_HOST_WRITE_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT|(initial?VK_PIPELINE_STAGE_HOST_BIT:0),
        final?VK_PIPELINE_STAGE_HOST_BIT:VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&barrier,0,nullptr,0,nullptr);
}
class VulkanDevice final : public NativeDevice {
    std::shared_ptr<VulkanOwner> owner_=std::make_shared<VulkanOwner>();
public:
    explicit VulkanDevice(const Options& options) {
        owner_->instance=std::make_shared<Instance>();
        auto all=physical_devices(*owner_->instance);
        require(options.adapter_index<all.size(),ErrorCode::unavailable,"Vulkan adapter index is unavailable");
        auto physical=owner_->physical=all[options.adapter_index];
        adapter=describe(physical,options.adapter_index);
        vkGetPhysicalDeviceProperties(physical,&owner_->properties);
        vkGetPhysicalDeviceMemoryProperties(physical,&owner_->memory);
        std::uint32_t count=0; vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,nullptr);
        std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical,&count,families.data());
        bool found=false;
        for(std::uint32_t i=0;i<count;++i) if(families[i].queueCount&&(families[i].queueFlags&VK_QUEUE_COMPUTE_BIT)) {
            owner_->family=i; found=true; break;
        }
        require(found,ErrorCode::unavailable,"Vulkan adapter has no compute queue");
        checked(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,nullptr),"enumerate Vulkan device extensions");
        std::vector<VkExtensionProperties> available(count);
        checked(vkEnumerateDeviceExtensionProperties(physical,nullptr,&count,available.data()),"enumerate Vulkan device extensions");
        std::vector<const char*> extensions;
        for(const auto& e:available) if(std::strcmp(e.extensionName,"VK_KHR_portability_subset")==0) extensions.push_back("VK_KHR_portability_subset");
        float priority=1;
        VkDeviceQueueCreateInfo queue{}; queue.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queue.queueFamilyIndex=owner_->family; queue.queueCount=1; queue.pQueuePriorities=&priority;
        VkDeviceCreateInfo settings{}; settings.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        settings.queueCreateInfoCount=1; settings.pQueueCreateInfos=&queue;
        settings.enabledExtensionCount=static_cast<std::uint32_t>(extensions.size()); settings.ppEnabledExtensionNames=extensions.data();
        checked(vkCreateDevice(physical,&settings,nullptr,&owner_->device),"create Vulkan device");
        vkGetDeviceQueue(owner_->device,owner_->family,0,&owner_->queue);
        const auto& l=owner_->properties.limits;
        limits.max_buffer_bytes=l.maxStorageBufferRange;
        limits.max_bindings=std::min(16u,std::min(l.maxPerStageDescriptorStorageBuffers,l.maxDescriptorSetStorageBuffers));
        limits.max_constants_bytes=std::min(128u,l.maxPushConstantsSize);
        limits.max_groups={l.maxComputeWorkGroupCount[0],l.maxComputeWorkGroupCount[1],l.maxComputeWorkGroupCount[2]};
        limits.max_group_size={l.maxComputeWorkGroupSize[0],l.maxComputeWorkGroupSize[1],l.maxComputeWorkGroupSize[2]};
        limits.max_threads_per_group=l.maxComputeWorkGroupInvocations;
        limits.binding_offset_alignment=std::max(VkDeviceSize(4),l.minStorageBufferOffsetAlignment);
    }
    std::unique_ptr<NativeBuffer> buffer(BufferDesc desc) override {
        auto value=std::make_unique<VulkanBuffer>(owner_);
        VkBufferCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        info.size=desc.bytes; info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        const auto flags=static_cast<unsigned>(desc.usage);
        if(flags&1) info.usage|=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if(flags&2) info.usage|=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if(flags&4) info.usage|=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        checked(vkCreateBuffer(owner_->device,&info,nullptr,&value->buffer),"create Vulkan buffer");
        VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(owner_->device,value->buffer,&requirements);
        auto selected=owner_->memory.memoryTypeCount;
        int best=-1;
        for(std::uint32_t i=0;i<owner_->memory.memoryTypeCount;++i) {
            if(!(requirements.memoryTypeBits&(1u<<i))) continue;
            const auto flags=owner_->memory.memoryTypes[i].propertyFlags;
            if(desc.memory==Memory::shared&&!(flags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) continue;
            int score=0;
            if(desc.memory==Memory::shared) score+=(flags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)?2:0;
            else score+=(flags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)?2:0;
            if(score>best) {selected=i;best=score;}
        }
        require(selected<owner_->memory.memoryTypeCount,ErrorCode::unavailable,"no Vulkan memory type supports this buffer");
        VkMemoryAllocateInfo allocation{}; allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocation.allocationSize=requirements.size; allocation.memoryTypeIndex=selected;
        checked(vkAllocateMemory(owner_->device,&allocation,nullptr,&value->memory),"allocate Vulkan buffer memory");
        checked(vkBindBufferMemory(owner_->device,value->buffer,value->memory,0),"bind Vulkan buffer memory");
        value->allocated=requirements.size;
        value->coherent=(owner_->memory.memoryTypes[selected].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
        if(desc.memory==Memory::shared) checked(vkMapMemory(owner_->device,value->memory,0,VK_WHOLE_SIZE,0,&value->mapped),"map Vulkan buffer");
        return value;
    }
    std::unique_ptr<NativePipeline> pipeline(const ComputeDesc& desc) override {
        auto value=std::make_unique<VulkanPipeline>(owner_);
        const auto device=owner_->device;
        VkShaderModuleCreateInfo shader{}; shader.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shader.codeSize=desc.spirv.size()*4; shader.pCode=desc.spirv.data();
        checked(vkCreateShaderModule(device,&shader,nullptr,&value->shader),"create Vulkan shader module");
        std::vector<VkDescriptorSetLayoutBinding> bindings(desc.bindings.size());
        for(std::uint32_t i=0;i<bindings.size();++i) bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
        VkDescriptorSetLayoutCreateInfo layout{}; layout.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layout.bindingCount=static_cast<std::uint32_t>(bindings.size()); layout.pBindings=bindings.data();
        checked(vkCreateDescriptorSetLayout(device,&layout,nullptr,&value->descriptors),"create Vulkan descriptor layout");
        VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,desc.constants_bytes};
        VkPipelineLayoutCreateInfo pipeline_layout{}; pipeline_layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pipeline_layout.setLayoutCount=1; pipeline_layout.pSetLayouts=&value->descriptors;
        pipeline_layout.pushConstantRangeCount=desc.constants_bytes?1:0; pipeline_layout.pPushConstantRanges=&push;
        checked(vkCreatePipelineLayout(device,&pipeline_layout,nullptr,&value->layout),"create Vulkan pipeline layout");
        VkComputePipelineCreateInfo info{}; info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; info.stage.module=value->shader; info.stage.pName=desc.vulkan_entry.c_str(); info.layout=value->layout;
        checked(vkCreateComputePipelines(device,VK_NULL_HANDLE,1,&info,nullptr,&value->pipeline),"create Vulkan compute pipeline");
        return value;
    }
    std::unique_ptr<NativeSubmission> submit(const std::vector<Command>& commands) override {
        auto job=std::make_unique<VulkanSubmission>(owner_);
        const auto device=owner_->device;
        VkCommandPoolCreateInfo pool{}; pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT; pool.queueFamilyIndex=owner_->family;
        checked(vkCreateCommandPool(device,&pool,nullptr,&job->pool),"create Vulkan command pool");
        VkCommandBufferAllocateInfo allocate{}; allocate.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate.commandPool=job->pool; allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount=1;
        VkCommandBuffer command=VK_NULL_HANDLE; checked(vkAllocateCommandBuffers(device,&allocate,&command),"allocate Vulkan command buffer");
        std::uint64_t sets=0,bindings=0;
        for(const auto& c:commands) if(const auto* d=std::get_if<Dispatch>(&c)) {++sets;bindings+=d->bindings.size();}
        require(sets<=UINT32_MAX&&bindings<=UINT32_MAX,ErrorCode::limit_exceeded,"Vulkan descriptor count exceeds uint32 addressing");
        if(sets) {
            VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,static_cast<std::uint32_t>(bindings)};
            VkDescriptorPoolCreateInfo descriptors{}; descriptors.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            descriptors.maxSets=static_cast<std::uint32_t>(sets); descriptors.poolSizeCount=1; descriptors.pPoolSizes=&size;
            checked(vkCreateDescriptorPool(device,&descriptors,nullptr,&job->descriptors),"create Vulkan descriptor pool");
        }
        VkCommandBufferBeginInfo begin{}; begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        checked(vkBeginCommandBuffer(command,&begin),"begin Vulkan commands");
        memory_barrier(command,true);
        for(const auto& c:commands) {
            if(const auto* copy=std::get_if<Copy>(&c)) {
                VkBufferCopy region{copy->source_offset,copy->destination_offset,copy->bytes};
                vkCmdCopyBuffer(command,static_cast<VulkanBuffer&>(*copy->source->native).buffer,
                    static_cast<VulkanBuffer&>(*copy->destination->native).buffer,1,&region);
            } else if(const auto* fill=std::get_if<Fill>(&c)) {
                vkCmdFillBuffer(command,static_cast<VulkanBuffer&>(*fill->buffer->native).buffer,fill->offset,fill->bytes,fill->pattern);
            } else if(const auto* dispatch=std::get_if<Dispatch>(&c)) {
                auto& pipeline=static_cast<VulkanPipeline&>(*dispatch->pipeline->native);
                VkDescriptorSetAllocateInfo allocate{}; allocate.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocate.descriptorPool=job->descriptors; allocate.descriptorSetCount=1; allocate.pSetLayouts=&pipeline.descriptors;
                VkDescriptorSet set=VK_NULL_HANDLE; checked(vkAllocateDescriptorSets(device,&allocate,&set),"allocate Vulkan descriptor set");
                std::vector<VkDescriptorBufferInfo> buffers(dispatch->bindings.size());
                std::vector<VkWriteDescriptorSet> writes(buffers.size());
                for(std::uint32_t i=0;i<buffers.size();++i) {
                    const auto& binding=dispatch->bindings[i];
                    buffers[i]={static_cast<VulkanBuffer&>(*binding.buffer->native).buffer,binding.offset,binding.bytes};
                    writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dstSet=set; writes[i].dstBinding=i;
                    writes[i].descriptorCount=1; writes[i].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].pBufferInfo=&buffers[i];
                }
                vkUpdateDescriptorSets(device,static_cast<std::uint32_t>(writes.size()),writes.data(),0,nullptr);
                vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.pipeline);
                vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline.layout,0,1,&set,0,nullptr);
                if(!dispatch->constants.empty()) vkCmdPushConstants(command,pipeline.layout,VK_SHADER_STAGE_COMPUTE_BIT,0,
                    static_cast<std::uint32_t>(dispatch->constants.size()),dispatch->constants.data());
                vkCmdDispatch(command,dispatch->groups.x,dispatch->groups.y,dispatch->groups.z);
            } else memory_barrier(command);
        }
        memory_barrier(command,false,true);
        checked(vkEndCommandBuffer(command),"end Vulkan commands");
        VkFenceCreateInfo fence{}; fence.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        checked(vkCreateFence(device,&fence,nullptr,&job->fence),"create Vulkan fence");
        VkSubmitInfo submit{}; submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount=1; submit.pCommandBuffers=&command;
        checked(vkQueueSubmit(owner_->queue,1,&submit,job->fence),"submit Vulkan commands");
        return job;
    }
    NativeContext context() const override {
        NativeContext c{}; c.backend=Backend::vulkan;
        c.instance=bits(owner_->instance->handle); c.physical_device=bits(owner_->physical);
        c.device=bits(owner_->device); c.queue=bits(owner_->queue); c.queue_family=owner_->family; return c;
    }
};
}
std::vector<Adapter> vulkan_adapters() {
    Instance instance; auto all=physical_devices(instance); std::vector<Adapter> result;
    for(auto physical:all) result.push_back(describe(physical,result.size()));
    return result;
}
std::unique_ptr<NativeDevice> vulkan_device(const Options& options) { return std::make_unique<VulkanDevice>(options); }
} // namespace ii::gpu::detail
