#include <mem.h>

#include <cstdint>
#include <cstdio>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",            \
                         __FILE__, __LINE__, #condition);                     \
            return 1;                                                        \
        }                                                                    \
    } while (false)

int main() {
    void *first = rtos_malloc(512U);
    void *middle = rtos_malloc(512U);
    void *last = rtos_malloc(512U);
    CHECK(first != nullptr && middle != nullptr && last != nullptr);

    rtos_free(middle);
    void *reused = rtos_malloc(256U);
    CHECK(reused == middle);

    rtos_free(first);
    rtos_free(reused);
    rtos_free(last);
    void *coalesced = rtos_malloc(3500U);
    CHECK(coalesced != nullptr);
    rtos_free(coalesced);

    {
        Mem aligned = Mem::aligned(64U, 300U);
        CHECK(aligned);
        CHECK((reinterpret_cast<uintptr_t>(aligned.ptr()) & 63U) == 0U);
    }

    void *after_aligned_free = rtos_malloc(3500U);
    CHECK(after_aligned_free != nullptr);
    CHECK(rtos_malloc(4096U) == nullptr);
    rtos_free(after_aligned_free);

    std::puts("baremetal allocator tests passed");
    return 0;
}
