/**
 * @brief Mem / MemBlock 实现 — 基于 RTOS 堆管理
 *
 * 根据所选 RTOS 使用对应的堆实现：
 * - FreeRTOS: pvPortMalloc / vPortFree (heap_4)
 * - RT-Thread: rt_malloc / rt_free (small_mem)
 */

#include <mem_alloc.h>
#include <cstring>

/* ═══════════════════════════════════════════════════════════════════════════
 *  Mem — 静态工具类
 * ═══════════════════════════════════════════════════════════════════════════ */

#if defined(CONFIG_RTOS_FREERTOS)
#include <FreeRTOS.h>

void *Mem::malloc(size_t size) {
    return pvPortMalloc(size);
}

void *Mem::calloc(size_t nitems, size_t size) {
    size_t total = nitems * size;
    void *p = pvPortMalloc(total);
    if (p) std::memset(p, 0, total);
    return p;
}

void *Mem::realloc(void *ptr, size_t size) {
    if (!ptr) return pvPortMalloc(size);
    if (size == 0) { vPortFree(ptr); return nullptr; }
    void *p = pvPortMalloc(size);
    if (p) { std::memcpy(p, ptr, size); vPortFree(ptr); }
    return p;
}

void Mem::free(void *ptr) {
    if (ptr) vPortFree(ptr);
}

void *Mem::aligned_alloc(size_t alignment, size_t size) {
    if (alignment <= portBYTE_ALIGNMENT) {
        return pvPortMalloc(size);
    }
    size_t total = size + alignment + sizeof(void *);
    void *raw = pvPortMalloc(total);
    if (!raw) return nullptr;
    uintptr_t addr = reinterpret_cast<uintptr_t>(raw) + sizeof(void *);
    addr = (addr + alignment - 1) & ~(alignment - 1);
    void *aligned = reinterpret_cast<void *>(addr);
    static_cast<void **>(aligned)[-1] = raw;
    return aligned;
}

void Mem::aligned_free(void *ptr) {
    if (ptr) {
        void *raw = static_cast<void **>(ptr)[-1];
        vPortFree((raw == ptr) ? ptr : raw);
    }
}

#elif defined(CONFIG_RTOS_RT_THREAD)
#include <rtthread.h>

void *Mem::malloc(size_t size) {
    return rt_malloc(size);
}

void *Mem::calloc(size_t nitems, size_t size) {
    return rt_calloc(nitems, size);
}

void *Mem::realloc(void *ptr, size_t size) {
    return rt_realloc(ptr, size);
}

void Mem::free(void *ptr) {
    if (ptr) rt_free(ptr);
}

void *Mem::aligned_alloc(size_t alignment, size_t size) {
    if (alignment <= RT_ALIGN_SIZE) {
        return rt_malloc(size);
    }
    size_t total = size + alignment + sizeof(void *);
    void *raw = rt_malloc(total);
    if (!raw) return nullptr;
    uintptr_t addr = reinterpret_cast<uintptr_t>(raw) + sizeof(void *);
    addr = (addr + alignment - 1) & ~(alignment - 1);
    void *aligned = reinterpret_cast<void *>(addr);
    static_cast<void **>(aligned)[-1] = raw;
    return aligned;
}

void Mem::aligned_free(void *ptr) {
    if (ptr) {
        void *raw = static_cast<void **>(ptr)[-1];
        rt_free((raw == ptr) ? ptr : raw);
    }
}

#else
#error "No RTOS selected. Set CONFIG_RTOS_FREERTOS or CONFIG_RTOS_RT_THREAD."
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 *  MemBlock — RAII 内存块
 * ═══════════════════════════════════════════════════════════════════════════ */

MemBlock::MemBlock(size_t size)
    : ptr_(Mem::malloc(size)), size_(size), aligned_(false) {}

MemBlock::MemBlock(size_t nitems, size_t size)
    : ptr_(Mem::calloc(nitems, size)), size_(nitems * size), aligned_(false) {}

MemBlock::MemBlock(void *p, size_t s, bool aligned)
    : ptr_(p), size_(s), aligned_(aligned) {}

MemBlock MemBlock::aligned(size_t alignment, size_t size) {
    void *p = Mem::aligned_alloc(alignment, size);
    return MemBlock(p, size, true);
}

MemBlock::~MemBlock() {
    if (aligned_) {
        Mem::aligned_free(ptr_);
    } else {
        Mem::free(ptr_);
    }
}

MemBlock::MemBlock(MemBlock &&other) noexcept
    : ptr_(other.ptr_), size_(other.size_), aligned_(other.aligned_) {
    other.ptr_ = nullptr;
    other.size_ = 0;
    other.aligned_ = false;
}

MemBlock &MemBlock::operator=(MemBlock &&other) noexcept {
    if (this != &other) {
        if (aligned_) {
            Mem::aligned_free(ptr_);
        } else {
            Mem::free(ptr_);
        }
        ptr_ = other.ptr_;
        size_ = other.size_;
        aligned_ = other.aligned_;
        other.ptr_ = nullptr;
        other.size_ = 0;
        other.aligned_ = false;
    }
    return *this;
}

void *MemBlock::ptr() { return ptr_; }
const void *MemBlock::ptr() const { return ptr_; }
size_t MemBlock::size() const { return size_; }
MemBlock::operator bool() const { return ptr_ != nullptr; }
