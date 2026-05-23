/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 specific kernel interface header
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_ARCH_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_ARCH_H_

#include <zephyr/arch/arm/thread.h>
#include <zephyr/arch/arm/exception.h>
#include <zephyr/arch/arm/irq.h>
#include <zephyr/arch/arm/misc.h>

#ifdef CONFIG_CPU_CORTEX_M
#include <zephyr/arch/arm/cortex_m/cpu.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASMLANGUAGE

/* Cortex-M specific exception codes */
enum k_fatal_error_reason_arch {
	K_ERR_ARM_MEM_GENERIC = 0,
	K_ERR_ARM_MEM_STACKING,
	K_ERR_ARM_MEM_UNSTACKING,
	K_ERR_ARM_MEM_DATA_ACCESS,
	K_ERR_ARM_MEM_INSTRUCTION_ACCESS,
	K_ERR_ARM_BUS_GENERIC,
	K_ERR_ARM_USAGE_GENERIC,
	K_ERR_ARM_USAGE_DIV_0,
	K_ERR_ARM_USAGE_UNALIGNED_ACCESS,
	K_ERR_ARM_USAGE_STACK_OVERFLOW,
	K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION,
};

#endif /* _ASMLANGUAGE */

/**
 * @brief Declare the ARCH_STACK_PTR_ALIGN
 */
#ifdef CONFIG_STACK_ALIGN_DOUBLE_WORD
#define ARCH_STACK_PTR_ALIGN 8
#else
#define ARCH_STACK_PTR_ALIGN 4
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_ARCH_H_ */
