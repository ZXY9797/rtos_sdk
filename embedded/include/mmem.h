#ifndef MDK_INCLUDE_MMEM_H
#define MDK_INCLUDE_MMEM_H
#include <stddef.h>
#include <stdint.h>
#include <dt-bindings/memory-attr/memory-attr.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_CAPS_GET       DT_MEM_ATTR_GET
#define MEM_CAP_DMA        DT_MEM_DMA
#define MEM_CAP_HIGHSPEED  DT_MEM_HIGHSPEED
#define MEM_CAP_NOCACHE    DT_MEM_NOCACHE

void *mmem_caps_malloc(size_t size, uint32_t caps);
void *mmem_caps_calloc(size_t nitems, size_t size, uint32_t caps);
void *mmem_malloc(size_t size);
void *mmem_calloc(size_t nitems, size_t size);
void *mmem_realloc(void* ptr, size_t size);
void mmem_free(void* ptr);

void *mmem_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
void mmem_aligned_free(void* ptr);

#ifdef __cplusplus
}
#endif

#endif
