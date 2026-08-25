#include <mem.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#if !defined(CONFIG_OSAL_BAREMETAL_HEAP_SIZE)
#define CONFIG_OSAL_BAREMETAL_HEAP_SIZE 4096
#endif

namespace {

struct alignas(std::max_align_t) HeapBlock {
    size_t size;
    HeapBlock *next;
    bool free;
};

#if CONFIG_OSAL_BAREMETAL_HEAP_SIZE > 0
static_assert(CONFIG_OSAL_BAREMETAL_HEAP_SIZE
                  >= sizeof(HeapBlock) + alignof(std::max_align_t),
              "baremetal heap is too small for one allocation");
alignas(std::max_align_t)
uint8_t g_heap[CONFIG_OSAL_BAREMETAL_HEAP_SIZE];
HeapBlock *g_heap_head = nullptr;

class InterruptGuard {
public:
    InterruptGuard() {
#if defined(__arm__) || defined(__thumb__)
        asm volatile("mrs %0, primask" : "=r"(state_) :: "memory");
        asm volatile("cpsid i" ::: "memory");
#endif
    }

    ~InterruptGuard() {
#if defined(__arm__) || defined(__thumb__)
        asm volatile("msr primask, %0" :: "r"(state_) : "memory");
#endif
    }

    InterruptGuard(const InterruptGuard &) = delete;
    InterruptGuard &operator=(const InterruptGuard &) = delete;

private:
    uint32_t state_ {0U};
};

size_t align_up(size_t value, size_t align) {
    return (value + align - 1U) & ~(align - 1U);
}

void initialize_heap() {
    if (g_heap_head != nullptr) return;
    g_heap_head = new (g_heap) HeapBlock {
        sizeof(g_heap) - sizeof(HeapBlock), nullptr, true};
}

uint8_t *payload(HeapBlock *block) {
    return reinterpret_cast<uint8_t *>(block) + sizeof(HeapBlock);
}

void merge_with_next(HeapBlock &block) {
    while (block.next != nullptr && block.next->free) {
        block.size += sizeof(HeapBlock) + block.next->size;
        block.next = block.next->next;
    }
}
#endif

} // namespace

void *rtos_malloc(size_t size) {
#if CONFIG_OSAL_BAREMETAL_HEAP_SIZE > 0
    if (size == 0U) return nullptr;

    const size_t align = alignof(std::max_align_t);
    if (size > SIZE_MAX - (align - 1U)) return nullptr;
    const size_t aligned_size = align_up(size, align);
    const InterruptGuard guard;
    initialize_heap();

    for (HeapBlock *block = g_heap_head;
         block != nullptr; block = block->next) {
        if (!block->free || block->size < aligned_size) continue;

        const size_t remainder = block->size - aligned_size;
        if (remainder >= sizeof(HeapBlock) + align) {
            auto *next = new (payload(block) + aligned_size) HeapBlock {
                remainder - sizeof(HeapBlock), block->next, true};
            block->next = next;
            block->size = aligned_size;
        }
        block->free = false;
        return payload(block);
    }
    return nullptr;
#else
    (void)size;
    return nullptr;
#endif
}

void rtos_free(void *pointer) {
#if CONFIG_OSAL_BAREMETAL_HEAP_SIZE > 0
    if (pointer == nullptr) return;

    const InterruptGuard guard;
    initialize_heap();
    HeapBlock *previous = nullptr;
    for (HeapBlock *block = g_heap_head;
         block != nullptr; previous = block, block = block->next) {
        if (payload(block) != pointer) continue;
        if (block->free) return;

        block->free = true;
        merge_with_next(*block);
        if (previous != nullptr && previous->free) {
            merge_with_next(*previous);
        }
        return;
    }
#else
    (void)pointer;
#endif
}

Mem::Mem(size_t size)
    : ptr_(rtos_malloc(size)), size_(size), aligned_(false) {}

Mem::Mem(size_t nitems, size_t size)
    : ptr_((size == 0U || nitems <= SIZE_MAX / size)
               ? rtos_malloc(nitems * size) : nullptr),
      size_((size == 0U || nitems <= SIZE_MAX / size) ? nitems * size : 0U),
      aligned_(false) {
    if (ptr_) std::memset(ptr_, 0, size_);
}

Mem::Mem(void *p, size_t s, bool aligned)
    : ptr_(p), size_(s), aligned_(aligned) {}

Mem Mem::aligned(size_t alignment, size_t size) {
    if (alignment < alignof(void *)) alignment = alignof(void *);
    if ((alignment & (alignment - 1U)) != 0U
        || size > SIZE_MAX - (alignment - 1U) - sizeof(void *)) {
        return Mem(nullptr, 0U, true);
    }
    const size_t total = size + alignment - 1U + sizeof(void *);
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
