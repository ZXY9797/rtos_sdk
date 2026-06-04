#ifndef RTOS_SDK_INCLUDE_MEM_ALLOC_H_
#define RTOS_SDK_INCLUDE_MEM_ALLOC_H_

#include <cstddef>
#include <cstdint>

/**
 * @brief 静态内存分配工具类 — 底层分配原语
 *
 * 所有方法为 static，底层调用 RTOS 堆管理（FreeRTOS heap_4 / RT-Thread small_mem）。
 * 禁止实例化。
 */
class Mem {
public:
    static void *malloc(size_t size);
    static void *calloc(size_t nitems, size_t size);
    static void *realloc(void *ptr, size_t size);
    static void  free(void *ptr);
    static void *aligned_alloc(size_t alignment, size_t size);
    static void  aligned_free(void *ptr);

    Mem() = delete;
    ~Mem() = delete;
    Mem(const Mem &) = delete;
    Mem &operator=(const Mem &) = delete;
};

/**
 * @brief RAII 内存块 — 构造分配，析构释放
 *
 * 支持移动语义，禁止拷贝。通过 aligned() 工厂方法创建对齐分配。
 */
class MemBlock {
public:
    explicit MemBlock(size_t size);
    MemBlock(size_t nitems, size_t size);
    static MemBlock aligned(size_t alignment, size_t size);

    ~MemBlock();

    MemBlock(MemBlock &&other) noexcept;
    MemBlock &operator=(MemBlock &&other) noexcept;
    MemBlock(const MemBlock &) = delete;
    MemBlock &operator=(const MemBlock &) = delete;

    void *ptr();
    const void *ptr() const;
    size_t size() const;
    explicit operator bool() const;

private:
    MemBlock(void *p, size_t s, bool aligned);
    void *ptr_ = nullptr;
    size_t size_ = 0;
    bool aligned_ = false;
};

#endif /* RTOS_SDK_INCLUDE_MEM_ALLOC_H_ */
