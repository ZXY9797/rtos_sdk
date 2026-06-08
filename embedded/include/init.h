#pragma once

#include <toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*initcall_fn_t)(void);

struct initcall_entry {
	initcall_fn_t fn;
};

/*
 * Initcall levels.
 *
 * PRE_KERNEL levels run before osal_init(), so they must not use RTOS services.
 *   EARLY        - earliest hardware setup
 *   PRE_KERNEL_1 - core pre-kernel infrastructure
 *   PRE_KERNEL_2 - low-level HAL/bus controllers, such as UART/SPI/DMA
 *   PRE_KERNEL_3 - devices that depend on PRE_KERNEL_2 drivers but do not
 *                  need RTOS services, such as simple SPI/I2C sensors
 *
 * POST_KERNEL levels run after osal_init().
 *   POST_KERNEL  - drivers that need RTOS services
 *   APPLICATION  - application-level initialization
 */

enum init_level {
	INITCALL_LEVEL_EARLY = 0,
	INITCALL_LEVEL_PRE_KERNEL_1,
	INITCALL_LEVEL_PRE_KERNEL_2,
	INITCALL_LEVEL_PRE_KERNEL_3,
	INITCALL_LEVEL_POST_KERNEL,
	INITCALL_LEVEL_APPLICATION,
	INITCALL_LEVEL_COUNT,
};

/*
 * Section name: .z_init_<LEVEL>_P_<PRIORITY>_<COUNTER>
 * The linker scripts keep and sort these sections within each init level.
 */
#define _STRINGIFY(x) #x
#define _STRINGIFY2(x) _STRINGIFY(x)

#define _LEVEL_NAME_FOR_INITCALL_LEVEL_EARLY         "EARLY"
#define _LEVEL_NAME_FOR_INITCALL_LEVEL_PRE_KERNEL_1  "PRE_KERNEL_1"
#define _LEVEL_NAME_FOR_INITCALL_LEVEL_PRE_KERNEL_2  "PRE_KERNEL_2"
#define _LEVEL_NAME_FOR_INITCALL_LEVEL_PRE_KERNEL_3  "PRE_KERNEL_3"
#define _LEVEL_NAME_FOR_INITCALL_LEVEL_POST_KERNEL   "POST_KERNEL"
#define _LEVEL_NAME_FOR_INITCALL_LEVEL_APPLICATION   "APPLICATION"
#define _LEVEL_NAME_FOR(l)  _LEVEL_NAME_FOR2(l)
#define _LEVEL_NAME_FOR2(l) _LEVEL_NAME_FOR_ ## l

#define _INITCALL_SECTION(level, prio, counter)                              \
	".z_init_" _LEVEL_NAME_FOR(level) "_P_" _STRINGIFY2(prio) "_"          \
	_STRINGIFY2(counter)

#define _INITCALL_VAR2(counter)  _initcall_entry_##counter
#define _INITCALL_VAR(counter)   _INITCALL_VAR2(counter)

#define _SYS_INIT_ENTRY_WITH_ID(fn, level, prio, id)                         \
	static const Z_DECL_ALIGN(struct initcall_entry)                         \
		__attribute__((section(_INITCALL_SECTION(level, prio, id)))) __used \
		_INITCALL_VAR(id) = { reinterpret_cast<initcall_fn_t>(fn) }

#define _SYS_INIT_ENTRY(fn, level, prio)                                     \
	_SYS_INIT_ENTRY_WITH_ID(fn, level, prio, __COUNTER__)

/* Default priorities. */
#define _INITCALL_PRIO_DRIVER  25
#define _INITCALL_PRIO_APP     50

/**
 * SYS_INIT helper.
 *
 * Usage:
 *   SYS_INIT(my_init);
 *   SYS_INIT(my_init, INITCALL_LEVEL_PRE_KERNEL_2);
 *   SYS_INIT(my_init, INITCALL_LEVEL_PRE_KERNEL_3, 25);
 */
#define SYS_INIT_1(fn) \
	_SYS_INIT_ENTRY(fn, INITCALL_LEVEL_PRE_KERNEL_2, _INITCALL_PRIO_DRIVER)

#define SYS_INIT_2(fn, level) \
	_SYS_INIT_ENTRY(fn, level, _INITCALL_PRIO_DRIVER)

#define SYS_INIT_3(fn, level, prio) \
	_SYS_INIT_ENTRY(fn, level, prio)

/* Variadic dispatch */
#define SYS_INIT_GET(_1, _2, _3, NAME, ...) NAME
#define SYS_INIT(...) SYS_INIT_GET(__VA_ARGS__, SYS_INIT_3, SYS_INIT_2, SYS_INIT_1)(__VA_ARGS__)

#ifdef __cplusplus
}
#endif
