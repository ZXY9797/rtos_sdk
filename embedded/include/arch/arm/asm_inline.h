#ifndef ZEPHYR_INCLUDE_ARCH_ARM_ASM_INLINE_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_ASM_INLINE_H_

/*
 * The file must not be included directly
 * Include kernel.h instead
 */

#if defined(__GNUC__) || defined(__ICCARM__)
#include <arch/arm/asm_inline_gcc.h>
#else
#error Unknown toolchain in asm_inline.h
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_ASM_INLINE_H_ */
