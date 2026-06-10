#include <assert.h>
#include <log.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 默认断言失败处理函数
 *
 * 打印断言表达式、函数名、行号，然后死循环。
 * 死循环便于调试器（J-Link/Ozone）抓取现场。
 */
void hal_assert_handler(const char *expr, const char *func, int line)
{
    LOGE("ASSERT", "!!! ASSERT FAILED !!!");
    LOGE("ASSERT", "  Expression: %s", expr);
    LOGE("ASSERT", "  Function:   %s", func);
    LOGE("ASSERT", "  Line:       %d", line);

    /* 死循环，便于调试器抓现场 */
    while (true) {
        /* 可选: 触发调试器断点 */
        #if defined(__ARM_ARCH_7M__) || defined(__ARM_ARCH_7EM__) || \
            defined(__ARM_ARCH_8M_MAIN__)
        __asm volatile("bkpt #0");
        #endif
    }
}

#ifdef __cplusplus
}
#endif
