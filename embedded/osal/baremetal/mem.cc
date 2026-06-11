#include <mem.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {

alignas(std::max_align_t) uint8_t g_heap[4096];
size_t g_heap_used = 0;

size_t align_up(size_t value, size_t align) {
    return (value + align - 1U) & ~(align - 1U);
}

} // namespace

void *rtos_malloc(size_t size) {
    if (size == 0) return nullptr;

    const size_t align = alignof(std::max_align_t);
    const size_t used = align_up(g_heap_used, align);
    const size_t next = used + align_up(size, align);
    if (next > sizeof(g_heap)) return nullptr;

    g_heap_used = next;
    return &g_heap[used];
}

void rtos_free(void *) {
}

Mem::Mem(size_t size)
    : ptr_(rtos_malloc(size)), size_(size), aligned_(false) {}

Mem::Mem(size_t nitems, size_t size)
    : ptr_(rtos_malloc(nitems * size)), size_(nitems * size), aligned_(false) {
    if (ptr_) std::memset(ptr_, 0, size_);
}

Mem::Mem(void *p, size_t s, bool aligned)
    : ptr_(p), size_(s), aligned_(aligned) {}

Mem Mem::aligned(size_t alignment, size_t size) {
    const size_t total = size + alignment + sizeof(void *);
    void *raw = rtos_malloc(total);
    if (!raw) return Mem(nullptr, 0, true);

    uintptr_t addr = reinterpret_cast<uintptr_t>(raw) + sizeof(void *);
    addr = (addr + alignment - 1U) & ~(static_cast<uintptr_t>(alignment) - 1U);
    auto *aligned = reinterpret_cast<void *>(addr);
    static_cast<void **>(aligned)[-1] = raw;
    return Mem(aligned, size, true);
}

Mem::~Mem() {
    if (ptr_) rtos_free(aligned_ ? static_cast<void **>(ptr_)[-1] : ptr_);
}

Mem::Mem(Mem &&other) noexcept
    : ptr_(other.ptr_), size_(other.size_), aligned_(other.aligned_) {
    other.ptr_ = nullptr;
    other.size_ = 0;
    other.aligned_ = false;
}

Mem &Mem::operator=(Mem &&other) noexcept {
    if (this != &other) {
        if (ptr_) rtos_free(aligned_ ? static_cast<void **>(ptr_)[-1] : ptr_);
        ptr_ = other.ptr_;
        size_ = other.size_;
        aligned_ = other.aligned_;
        other.ptr_ = nullptr;
        other.size_ = 0;
        other.aligned_ = false;
    }
    return *this;
}

