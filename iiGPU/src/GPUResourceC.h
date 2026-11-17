#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct iigpu_resource_device iigpu_resource_device;
typedef struct iigpu_resource_buffer iigpu_resource_buffer;
typedef struct iigpu_resource_manager iigpu_resource_manager;
typedef struct iigpu_compute_program {
    const char* metal_source;
    const char* metal_entry;
    const void* metal_library;
    size_t metal_library_size;
    const void* spirv;
    size_t spirv_size;
    const char* vulkan_entry;
    uint32_t group_size_x;
    const void* constants;
    size_t constants_size;
} iigpu_compute_program;
/* 0 succeeds; other codes are 1 + ii::gpu::ErrorCode. */
int iigpu_resource_device_create(int backend,uint64_t budget,iigpu_resource_device** out);
void iigpu_resource_device_destroy(iigpu_resource_device* device);
int iigpu_resource_device_budget(iigpu_resource_device* device,uint64_t* out);
int iigpu_resource_device_set_budget(iigpu_resource_device* device,uint64_t budget);
int iigpu_resource_device_allocated(iigpu_resource_device* device,uint64_t* out);
int iigpu_resource_buffer_create(iigpu_resource_device* device,uint64_t size,int device_memory,iigpu_resource_buffer** out);
void iigpu_resource_buffer_destroy(iigpu_resource_buffer* buffer);
int iigpu_resource_buffer_size(iigpu_resource_buffer* buffer,uint64_t* out);
int iigpu_resource_buffer_read(iigpu_resource_buffer* buffer,uint64_t offset,void* data,size_t length);
int iigpu_resource_buffer_write(iigpu_resource_buffer* buffer,uint64_t offset,const void* data,size_t length);
int iigpu_resource_buffer_transfer(iigpu_resource_device* destination,iigpu_resource_buffer* source,
                                    int move,int device_memory,uint64_t chunk_bytes,iigpu_resource_buffer** out);
int iigpu_resource_manager_create(int backend, uint64_t memory_budget, size_t max_devices,
                                   uint32_t max_items_per_chunk, iigpu_resource_manager** out);
void iigpu_resource_manager_destroy(iigpu_resource_manager* manager);
int iigpu_resource_manager_count(iigpu_resource_manager* manager, size_t* out);
/* Writes output only when every selected GPU chunk completes successfully. */
int iigpu_resource_manager_execute(iigpu_resource_manager* manager, const iigpu_compute_program* program,
    const void* input, size_t input_size, size_t input_stride,
    void* output, size_t output_size, size_t output_stride);
#ifdef __cplusplus
}
#endif
