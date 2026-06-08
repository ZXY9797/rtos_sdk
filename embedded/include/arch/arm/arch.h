#ifndef ZEPHYR_INCLUDE_ARCH_ARM_ARCH_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_ARCH_H_

#include <devicetree.h>
#include <arch/arm/exception.h>
#include <arch/arm/irq.h>
#include <arch/arm/asm_inline.h>

#ifdef CONFIG_CPU_CORTEX_M
#include <arch/arm/cortex_m/cpu.h>
#include <arch/arm/cortex_m/memory_map.h>
#elif defined(CONFIG_CPU_AARCH32_CORTEX_R) || defined(CONFIG_CPU_AARCH32_CORTEX_A)
#include <arch/arm/cortex_a_r/cpu.h>
#include <arch/arm/cortex_a_r/sys_io.h>
#if defined(CONFIG_AARCH32_ARMV8_R) || defined(CONFIG_CPU_CORTEX_A7)
#include <arch/arm/cortex_a_r/lib_helpers.h>
#include <arch/arm/cortex_a_r/armv7_v8_timer.h>
#else
#include <arch/arm/cortex_a_r/timer.h>
#endif
#endif


#ifndef _ASMLANGUAGE

#endif /* _ASMLANGUAGE */

/**
 * @brief Declare the ARCH_STACK_PTR_ALIGN
 *
 * Denotes the required alignment of the stack pointer on public API
 * boundaries
 *
 */
#ifdef CONFIG_STACK_ALIGN_DOUBLE_WORD
#define ARCH_STACK_PTR_ALIGN 8
#else
#define ARCH_STACK_PTR_ALIGN 4
#endif

/**
 * @brief Declare a minimum MPU guard alignment and size
 *
 * This specifies the minimum MPU guard alignment/size for the MPU. This
 * will be used to denote the guard section of the stack, if it exists.
 *
 * One key note is that this guard results in extra bytes being added to
 * the stack. APIs which give the stack ptr and stack size will take this
 * guard size into account.
 *
 * Stack is allocated, but initial stack pointer is at the end
 * (highest address).  Stack grows down to the actual allocation
 * address (lowest address).  Stack guard, if present, will comprise
 * the lowest MPU_GUARD_ALIGN_AND_SIZE bytes of the stack.
 *
 * The guard region must include enough space for an exception frame
 * below the trapping region as a stack fault will end up storing
 * the exception data (0x20 bytes) onto the stack below wherever
 * the stack pointer refers, even if that is within the guard region,
 * so we make sure the region is strictly larger than this size by
 * setting it to 0x40 (to respect any power-of-two requirements).
 *
 * As the stack grows down, it will reach the end of the stack when it
 * encounters either the stack guard region, or the stack allocation
 * address.
 *
 * ----------------------- <---- Stack allocation address + stack size +
 * |                     |            MPU_GUARD_ALIGN_AND_SIZE
 * |  Some thread data   | <---- Defined when thread is created
 * |        ...          |
 * |---------------------| <---- Actual initial stack ptr
 * |  Initial Stack Ptr  |       aligned to ARCH_STACK_PTR_ALIGN
 * |        ...          |
 * |        ...          |
 * |        ...          |
 * |        ...          |
 * |        ...          |
 * |        ...          |
 * |        ...          |
 * |        ...          |
 * |  Stack Ends         |
 * |---------------------- <---- Stack Buffer Ptr from API
 * |  MPU Guard,         |
 * |     if present      |
 * ----------------------- <---- Stack Allocation address
 *
 */
#if defined(CONFIG_MPU_STACK_GUARD)
/* make sure there's more than enough space for an exception frame */
#if CONFIG_ARM_MPU_REGION_MIN_ALIGN_AND_SIZE <= 0x20
#define MPU_GUARD_ALIGN_AND_SIZE 0x40
#else
#define MPU_GUARD_ALIGN_AND_SIZE CONFIG_ARM_MPU_REGION_MIN_ALIGN_AND_SIZE
#endif
#else
#define MPU_GUARD_ALIGN_AND_SIZE 0
#endif

/**
 * @brief Declare the MPU guard alignment and size for a thread stack
 *        that is using the Floating Point services.
 *
 * For threads that are using the Floating Point services under Shared
 * Registers (CONFIG_FPU_SHARING=y) mode, the exception stack frame may
 * contain both the basic stack frame and the FP caller-saved context,
 * upon exception entry. Therefore, a wide guard region is required to
 * guarantee that stack-overflow detection will always be successful.
 */
#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING) \
	&& defined(CONFIG_MPU_STACK_GUARD)
#if CONFIG_MPU_STACK_GUARD_MIN_SIZE_FLOAT <= 0x20
#define MPU_GUARD_ALIGN_AND_SIZE_FLOAT 0x40
#else
#define MPU_GUARD_ALIGN_AND_SIZE_FLOAT CONFIG_MPU_STACK_GUARD_MIN_SIZE_FLOAT
#endif
#else
#define MPU_GUARD_ALIGN_AND_SIZE_FLOAT 0
#endif

/**
 * @brief Define alignment of an MPU guard
 *
 * Minimum alignment of the start address of an MPU guard, depending on
 * whether the MPU architecture enforces a size (and power-of-two) alignment
 * requirement.
 */
#if defined(CONFIG_MPU_REQUIRES_POWER_OF_TWO_ALIGNMENT)
#define Z_MPU_GUARD_ALIGN (MAX(MPU_GUARD_ALIGN_AND_SIZE, \
	MPU_GUARD_ALIGN_AND_SIZE_FLOAT))
#else
#define Z_MPU_GUARD_ALIGN MPU_GUARD_ALIGN_AND_SIZE
#endif

#ifdef CONFIG_MPU_STACK_GUARD
/* Kernel-only stacks need an MPU guard region programmed at the beginning of
 * the stack object, so align the object appropriately.
 */
#define ARCH_KERNEL_STACK_RESERVED	MPU_GUARD_ALIGN_AND_SIZE
#define ARCH_KERNEL_STACK_OBJ_ALIGN	Z_MPU_GUARD_ALIGN
#endif

/* On arm, all MPU guards are carve-outs. */
#define ARCH_THREAD_STACK_RESERVED 0

/* Legacy case: retain containing extern "C" with C++ */
#ifdef CONFIG_ARM_MPU
#ifdef CONFIG_CPU_HAS_ARM_MPU
#include <arch/arm/mpu/arm_mpu.h>
#endif /* CONFIG_CPU_HAS_ARM_MPU */
#ifdef CONFIG_CPU_HAS_NXP_SYSMPU
#include <arch/arm/mpu/nxp_mpu.h>
#endif /* CONFIG_CPU_HAS_NXP_SYSMPU */
#endif /* CONFIG_ARM_MPU */
#ifdef CONFIG_ARM_AARCH32_MMU
#include <arch/arm/mmu/arm_mmu.h>
#endif /* CONFIG_ARM_AARCH32_MMU */


#endif
