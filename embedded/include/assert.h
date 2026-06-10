#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运行时断言失败处理函数
 *
 * 默认实现打印断言信息并死循环，便于调试器抓现场。
 * 用户可在应用层覆盖此函数实现自定义处理（如记录日志后复位）。
 */
void hal_assert_handler(const char *expr, const char *func, int line);

#ifdef __cplusplus
}
#endif

/**
 * @brief 运行时断言宏
 *
 * 在 Debug 模式下检查条件，失败时调用 hal_assert_handler。
 * Release 模式下（定义 NDEBUG）自动禁用，零开销。
 *
 * 用法:
 *   HAL_ASSERT(ptr != nullptr);
 *   HAL_ASSERT(m_initialized);
 */
#ifdef NDEBUG
#define HAL_ASSERT(expr) ((void)0)
#define HAL_ASSERT_MSG(expr, msg) ((void)0)
#else
#define HAL_ASSERT(expr) \
    do { \
        if (!(expr)) { \
            hal_assert_handler(#expr, __func__, __LINE__); \
        } \
    } while (0)
#define HAL_ASSERT_MSG(expr, msg) \
    do { \
        if (!(expr)) { \
            hal_assert_handler(msg " (" #expr ")", __func__, __LINE__); \
        } \
    } while (0)
#endif
