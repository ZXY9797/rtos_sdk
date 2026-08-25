#ifndef RTOS_SDK_INCLUDE_MEM_H_
#define RTOS_SDK_INCLUDE_MEM_H_

#include <arch/arm/cortex_m/fault.h>
#include <osal_types.h>
#include <new>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace embedded_memory {

[[noreturn]] inline void allocation_failed(size_t size,
                                           const char *operation)
{
    constexpr size_t max_detail =
        static_cast<size_t>(std::numeric_limits<int32_t>::max());
    const int32_t detail = static_cast<int32_t>(
        size <= max_detail ? size : max_detail);
    hal::fault::panic(hal::fault::FatalReason::AllocationFailure,
                      detail, operation, 0U);
}

} // namespace embedded_memory

// ─── 全局 new/delete → RTOS 堆 ────────────────────────────────────

inline void *operator new(size_t size) {
    void *ptr = rtos_malloc(size);
    if (ptr == nullptr) {
        embedded_memory::allocation_failed(size, "operator_new");
    }
    return ptr;
}
inline void *operator new[](size_t size) {
    void *ptr = rtos_malloc(size);
    if (ptr == nullptr) {
        embedded_memory::allocation_failed(size, "operator_new_array");
    }
    return ptr;
}
inline void *operator new(size_t size, const std::nothrow_t &) noexcept { return rtos_malloc(size); }
inline void *operator new[](size_t size, const std::nothrow_t &) noexcept { return rtos_malloc(size); }

inline void operator delete(void *ptr) noexcept { rtos_free(ptr); }
inline void operator delete[](void *ptr) noexcept { rtos_free(ptr); }
inline void operator delete(void *ptr, size_t) noexcept { rtos_free(ptr); }
inline void operator delete[](void *ptr, size_t) noexcept { rtos_free(ptr); }

// ─── Mem — RAII 内存块 ────────────────────────────────────────────

class Mem {
public:
    explicit Mem(size_t size);
    Mem(size_t nitems, size_t size);
    static Mem aligned(size_t alignment, size_t size);

    ~Mem();

    Mem(Mem &&other) noexcept;
    Mem &operator=(Mem &&other) noexcept;
    Mem(const Mem &) = delete;
    Mem &operator=(const Mem &) = delete;

    void *ptr() { return ptr_; }
    const void *ptr() const { return ptr_; }
    size_t size() const { return size_; }
    explicit operator bool() const { return ptr_ != nullptr; }

private:
    Mem(void *p, size_t s, bool aligned);
    void *ptr_ = nullptr;
    size_t size_ = 0;
    bool aligned_ = false;
};

#endif /* RTOS_SDK_INCLUDE_MEM_H_ */
