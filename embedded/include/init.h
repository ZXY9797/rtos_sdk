/**
 * @brief 初始化系统声明 (最小化版本)
 *
 * 供 device.h 等 arch 层代码使用。
 * 新代码应使用 init.h 中的 C++ 接口。
 */

#ifndef MSDK_INCLUDE_INIT_H_
#define MSDK_INCLUDE_INIT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化级别 */
#define PRE_KERNEL_1  0
#define PRE_KERNEL_2  1
#define POST_KERNEL   2
#define APPLICATION   3

/** 初始化优先级 */
#define CONFIG_APPLICATION_INIT_PRIORITY 90
#define CONFIG_KERNEL_INIT_PRIORITY 40

struct init_entry {
    int (*init)(void);
};

/**
 * @brief 简化的 SYS_INIT 宏
 *
 * 简化版本：定义一个调用包装器，可在启动时手动调用。
 * 后续可改为段注册机制。
 */
#define SYS_INIT(init_fn, level, prio) \
    __attribute__((used)) static void _sys_init_##init_fn(void) { (void)init_fn(); }

#ifdef __cplusplus
}
#endif

#endif /* MSDK_INCLUDE_INIT_H_ */
