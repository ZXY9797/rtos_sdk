/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_IRQ_H_
#define ZEPHYR_INCLUDE_IRQ_H_

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/* IRQ flags */
#define IRQ_ZERO_LATENCY (1 << 0)

/* Interrupt management functions */
extern unsigned int irq_lock(void);
extern void irq_unlock(unsigned int key);
extern void irq_disable(unsigned int irq);
extern void irq_enable(unsigned int irq);
extern void irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags);

/* Check if in ISR context */
extern bool k_is_in_isr(void);

/* Check if in preemptible thread */
extern bool k_is_preempt_thread(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_IRQ_H_ */
