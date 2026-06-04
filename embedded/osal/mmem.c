/**
 * @brief mmem 内存分配接口
 *
 * 根据所选 RTOS 使用对应的堆管理实现：
 * - FreeRTOS: pvPortMalloc / vPortFree (heap_4)
 * - RT-Thread: rt_malloc / rt_free
 */

#include <mmem.h>
#include <string.h>

#if defined(CONFIG_RTOS_FREERTOS)
/* ── FreeRTOS 实现 ──────────────────────────────────────────── */
#include <FreeRTOS.h>

void *mmem_malloc(size_t size) {
    return pvPortMalloc(size);
}

void *mmem_calloc(size_t nitems, size_t size) {
    size_t total = nitems * size;
    void *p = pvPortMalloc(total);
    if (p) memset(p, 0, total);
    return p;
}

void *mmem_realloc(void *ptr, size_t size) {
    if (!ptr) return pvPortMalloc(size);
    if (size == 0) { vPortFree(ptr); return NULL; }
    void *p = pvPortMalloc(size);
    if (p) { memcpy(p, ptr, size); vPortFree(ptr); }
    return p;
}

void mmem_free(void *ptr) {
    if (ptr) vPortFree(ptr);
}

void *mmem_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return pvPortMalloc(size);
}

void *mmem_caps_calloc(size_t nitems, size_t size, uint32_t caps) {
    (void)caps;
    return mmem_calloc(nitems, size);
}

void *mmem_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
    (void)caps;
    if (alignment <= portBYTE_ALIGNMENT) {
        return pvPortMalloc(size);
    }
    size_t total = size + alignment + sizeof(void *);
    void *raw = pvPortMalloc(total);
    if (!raw) return NULL;
    uintptr_t addr = (uintptr_t)raw + sizeof(void *);
    addr = (addr + alignment - 1) & ~(alignment - 1);
    void *aligned = (void *)addr;
    ((void **)aligned)[-1] = raw;
    return aligned;
}

void mmem_aligned_free(void *ptr) {
    if (ptr) {
        void *raw = ((void **)ptr)[-1];
        vPortFree((raw == ptr) ? ptr : raw);
    }
}

#elif defined(CONFIG_RTOS_RT_THREAD)
/* ── RT-Thread 实现 ─────────────────────────────────────────── */
#include <rtthread.h>

void *mmem_malloc(size_t size) {
    return rt_malloc(size);
}

void *mmem_calloc(size_t nitems, size_t size) {
    return rt_calloc(nitems, size);
}

void *mmem_realloc(void *ptr, size_t size) {
    return rt_realloc(ptr, size);
}

void mmem_free(void *ptr) {
    if (ptr) rt_free(ptr);
}

void *mmem_caps_malloc(size_t size, uint32_t caps) {
    (void)caps;
    return rt_malloc(size);
}

void *mmem_caps_calloc(size_t nitems, size_t size, uint32_t caps) {
    (void)caps;
    return rt_calloc(nitems, size);
}

void *mmem_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps) {
    (void)caps;
    if (alignment <= RT_ALIGN_SIZE) {
        return rt_malloc(size);
    }
    size_t total = size + alignment + sizeof(void *);
    void *raw = rt_malloc(total);
    if (!raw) return NULL;
    uintptr_t addr = (uintptr_t)raw + sizeof(void *);
    addr = (addr + alignment - 1) & ~(alignment - 1);
    void *aligned = (void *)addr;
    ((void **)aligned)[-1] = raw;
    return aligned;
}

void mmem_aligned_free(void *ptr) {
    if (ptr) {
        void *raw = ((void **)ptr)[-1];
        rt_free((raw == ptr) ? ptr : raw);
    }
}

#else
#error "No RTOS selected. Set CONFIG_RTOS_FREERTOS or CONFIG_RTOS_RT_THREAD."
#endif
