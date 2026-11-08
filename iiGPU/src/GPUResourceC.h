#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct iigpu_resource_device iigpu_resource_device;
typedef struct iigpu_resource_buffer iigpu_resource_buffer;
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
#ifdef __cplusplus
}
#endif
