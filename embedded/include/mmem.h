#ifndef MSDK_INCLUDE_MMEM_H
#define MSDK_INCLUDE_MMEM_H

/**
 * @brief 统一内存分配接口 — 基于 FreeRTOS heap_4 实现
 *
 * 提供标准 malloc/free 语义的内存分配函数。
 * 所有分配来自 FreeRTOS 管理的堆，线程安全。
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 兼容旧接口的 caps 宏（不再区分内存区域） */
#define MEM_CAP_DMA        0
#define MEM_CAP_HIGHSPEED  0
#define MEM_CAP_NOCACHE    0

void *mmem_malloc(size_t size);
void *mmem_calloc(size_t nitems, size_t size);
void *mmem_realloc(void *ptr, size_t size);
void  mmem_free(void *ptr);

/* 带 caps 参数的版本（caps 参数被忽略，统一使用同一堆） */
void *mmem_caps_malloc(size_t size, uint32_t caps);
void *mmem_caps_calloc(size_t nitems, size_t size, uint32_t caps);
void *mmem_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps);
void  mmem_aligned_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif /* MSDK_INCLUDE_MMEM_H */
