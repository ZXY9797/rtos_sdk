#pragma once

#include <toolchain/gcc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*initcall_fn_t)(void);

struct initcall_entry {
    initcall_fn_t fn;
};

/*
 * Initcall levels — 数值越小越早执行
 * 链接脚本 SORT_BY_NAME 排序，使用零填充保证 10+ 不乱序
 */
#define INITCALL_LEVEL_EARLY       01   /* 早期硬件初始化（时钟、电源）    */
#define INITCALL_LEVEL_CORE        02   /* 核心基础设施（内存、中断）      */
#define INITCALL_LEVEL_SUBSYS      03   /* 子系统（DMA、总线控制器）       */
#define INITCALL_LEVEL_DRIVER      05   /* 设备驱动（UART、SPI、GPIO）    */
#define INITCALL_LEVEL_APP         08   /* 业务层初始化                    */
#define INITCALL_LEVEL_LATE        09   /* 最后执行                        */

/* 内部宏 */
#define _INITCALL_SECTION(level) .initcall.level
#define _INITCALL_VAR2(counter) _initcall_entry_##counter
#define _INITCALL_VAR(counter) _INITCALL_VAR2(counter)

/**
 * @brief 注册 initcall 函数
 *
 * 将函数指针放入 .initcall.<level> section，
 * run_initcalls() 启动时按 level 从小到大执行。
 *
 * 用法:
 *   static int my_init(void) { return 0; }
 *   DECLARE_INITCALL(my_init, INITCALL_LEVEL_DRIVER);
 */
#define DECLARE_INITCALL(fn, level)                                        \
    static const Z_DECL_ALIGN(struct initcall_entry)                       \
        Z_GENERIC_SECTION(_INITCALL_SECTION(level)) __used                 \
        _INITCALL_VAR(__COUNTER__) = { reinterpret_cast<initcall_fn_t>(fn) }

/**
 * @brief 便捷宏 — 自动选择默认 level
 *
 * 用法:
 *   SYS_INIT(my_init);                      // 默认 DRIVER level
 *   SYS_INIT(my_init, INITCALL_LEVEL_APP);  // 指定 level
 */
#define SYS_INIT_1(fn)              DECLARE_INITCALL(fn, INITCALL_LEVEL_DRIVER)
#define SYS_INIT_2(fn, level)       DECLARE_INITCALL(fn, level)
#define SYS_INIT_GET(_1, _2, NAME, ...) NAME
#define SYS_INIT(...) SYS_INIT_GET(__VA_ARGS__, SYS_INIT_2, SYS_INIT_1)(__VA_ARGS__)

void run_initcalls(void);

#ifdef __cplusplus
}
#endif
