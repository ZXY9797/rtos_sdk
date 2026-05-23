/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_THREAD_H_
#define ZEPHYR_INCLUDE_ARCH_THREAD_H_

/* Architecture-specific thread header */

/* Thread structure for ARM */
struct _thread_arch {
    uint32_t basepri;
    uint32_t swap_return_value;
#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
    struct _preempt_float preempt_float;
#endif
#if defined(CONFIG_USERSPACE)
    uint32_t priv_stack_start;
#endif
};

/* Callee-saved registers for ARM */
struct _callee_saved {
    uint32_t v1;  /* r4 */
    uint32_t v2;  /* r5 */
    uint32_t v3;  /* r6 */
    uint32_t v4;  /* r7 */
    uint32_t v5;  /* r8 */
    uint32_t v6;  /* r9 */
    uint32_t v7;  /* r10 */
    uint32_t v8;  /* r11 */
    uint32_t psp; /* r13 */
    uint32_t lr;  /* r14 */
};

#endif /* ZEPHYR_INCLUDE_ARCH_THREAD_H_ */
