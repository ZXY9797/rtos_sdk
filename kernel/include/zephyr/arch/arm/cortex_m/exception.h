/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM Cortex-M exception handling
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_CORTEX_M_EXCEPTION_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_CORTEX_M_EXCEPTION_H_

#include <zephyr/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Exception frame structure */
struct excep_frame {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
};

/* Exception frame for FPU */
struct excep_frame_fpu {
    uint32_t r0;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r12;
    uint32_t lr;
    uint32_t pc;
    uint32_t xpsr;
    uint32_t s0;
    uint32_t s1;
    uint32_t s2;
    uint32_t s3;
    uint32_t s4;
    uint32_t s5;
    uint32_t s6;
    uint32_t s7;
    uint32_t s8;
    uint32_t s9;
    uint32_t s10;
    uint32_t s11;
    uint32_t s12;
    uint32_t s13;
    uint32_t s14;
    uint32_t s15;
    uint32_t fpscr;
};

/* Exception numbers */
#define EXC_RESET              1
#define EXC_NMI                2
#define EXC_HARD_FAULT         3
#define EXC_MEM_MANAGE         4
#define EXC_BUS_FAULT          5
#define EXC_USAGE_FAULT        6
#define EXC_SVC                11
#define EXC_DEBUG_MONITOR      12
#define EXC_PENDSV             14
#define EXC_SYSTICK            15

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_CORTEX_M_EXCEPTION_H_ */
