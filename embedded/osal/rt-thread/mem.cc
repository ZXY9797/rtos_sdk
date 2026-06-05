/**
 * @brief Mem 实现 — RT-Thread
 */

#include <mem.h>
#include <cstring>
#include <rtthread.h>

// ─── 内部辅助 ─────────────────────────────────────────────────────

static void *rtos_calloc(size_t n, size_t size) { return rt_calloc(n, size); }

static void *rtos_aligned_alloc(size_t alignment, size_t size) {
    if (alignment <= RT_ALIGN_SIZE) return rt_malloc(size);
    size_t total = size + alignment + sizeof(void *);
    void *raw = rt_malloc(total);
    if (!raw) return nullptr;
    uintptr_t addr = reinterpret_cast<uintptr_t>(raw) + sizeof(void *);
    addr = (addr + alignment - 1) & ~(alignment - 1);
    auto *aligned = reinterpret_cast<void *>(addr);
    static_cast<void **>(aligned)[-1] = raw;
    return aligned;
}

static void rtos_aligned_free(void *ptr) {
    if (ptr) {
        void *raw = static_cast<void **>(ptr)[-1];
        rt_free((raw == ptr) ? ptr : raw);
    }
}

// ─── Mem ───────────────────────────────────────────────────────────

Mem::Mem(size_t size)
    : ptr_(rtos_malloc(size)), size_(size), aligned_(false) {}

Mem::Mem(size_t nitems, size_t size)
    : ptr_(rtos_calloc(nitems, size)), size_(nitems * size), aligned_(false) {}

Mem::Mem(void *p, size_t s, bool aligned)
    : ptr_(p), size_(s), aligned_(aligned) {}

Mem Mem::aligned(size_t alignment, size_t size) {
    return Mem(rtos_aligned_alloc(alignment, size), size, true);
}

Mem::~Mem() {
    if (aligned_) rtos_aligned_free(ptr_);
    else          rtos_free(ptr_);
}

Mem::Mem(Mem &&other) noexcept
    : ptr_(other.ptr_), size_(other.size_), aligned_(other.aligned_) {
    other.ptr_ = nullptr;
    other.size_ = 0;
    other.aligned_ = false;
}

Mem &Mem::operator=(Mem &&other) noexcept {
    if (this != &other) {
        if (aligned_) rtos_aligned_free(ptr_);
        else          rtos_free(ptr_);
        ptr_ = other.ptr_;
        size_ = other.size_;
        aligned_ = other.aligned_;
        other.ptr_ = nullptr;
        other.size_ = 0;
        other.aligned_ = false;
    }
    return *this;
}
