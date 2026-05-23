/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM Cortex-M interrupt management
 *
 * This module provides interrupt management functions for ARM Cortex-M
 * architectures, including zero latency interrupt support.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_IRQ_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_IRQ_H_

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IRQ flags */
#define IRQ_ZERO_LATENCY (1 << 0)

/* Number of IRQ priority bits */
#define NUM_IRQ_PRIO_BITS CONFIG_NUM_IRQ_PRIO_BITS

/* IRQ priority offset */
#define _IRQ_PRIO_OFFSET (1 << (8 - NUM_IRQ_PRIO_BITS))

/* Zero latency IRQ priority */
#define _EXC_ZERO_LATENCY_IRQS_PRIO 0

/* Architecture-specific IRQ functions */

/**
 * @brief Disable all interrupts on the current CPU.
 *
 * This routine disables all interrupts on the current CPU and returns
 * the previous interrupt state.
 *
 * @return The previous interrupt state.
 */
static inline unsigned int arch_irq_lock(void)
{
    unsigned int key;

    __asm__ volatile(
        "mrs %0, primask\n"
        "cpsid i\n"
        : "=r"(key)
        :
        : "memory"
    );

    return key;
}

/**
 * @brief Enable all interrupts on the current CPU.
 *
 * This routine enables all interrupts on the current CPU.
 *
 * @param key The previous interrupt state returned by arch_irq_lock().
 */
static inline void arch_irq_unlock(unsigned int key)
{
    __asm__ volatile(
        "msr primask, %0\n"
        :
        : "r"(key)
        : "memory"
    );
}

/**
 * @brief Disable an IRQ.
 *
 * This routine disables the specified IRQ.
 *
 * @param irq IRQ number.
 */
extern void arch_irq_disable(unsigned int irq);

/**
 * @brief Enable an IRQ.
 *
 * This routine enables the specified IRQ.
 *
 * @param irq IRQ number.
 */
extern void arch_irq_enable(unsigned int irq);

/**
 * @brief Set IRQ priority.
 *
 * This routine sets the priority of the specified IRQ.
 *
 * @param irq IRQ number.
 * @param prio Priority level.
 * @param flags IRQ flags (IRQ_ZERO_LATENCY for zero latency interrupts).
 */
extern void arch_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags);

/**
 * @brief Check if running in ISR context.
 *
 * This routine checks if the current execution context is an ISR.
 *
 * @return true if in ISR context, false otherwise.
 */
static inline bool arch_is_in_isr(void)
{
    /* Check IPSR register for exception number */
    uint32_t ipsr;

    __asm__ volatile("mrs %0, ipsr" : "=r"(ipsr));

    return (ipsr != 0);
}

/**
 * @brief CPU idle function.
 *
 * This routine puts the CPU into idle state.
 */
static inline void arch_cpu_idle(void)
{
    __asm__ volatile("wfi");
}

/**
 * @brief CPU atomic idle function.
 *
 * This routine atomically enables interrupts and puts the CPU into idle state.
 *
 * @param key The previous interrupt state returned by arch_irq_lock().
 */
static inline void arch_cpu_atomic_idle(unsigned int key)
{
    arch_irq_unlock(key);
    __asm__ volatile("wfi");
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_IRQ_H_ */
