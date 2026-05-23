/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_KERNEL_STRUCTS_H_
#define ZEPHYR_KERNEL_INCLUDE_KERNEL_STRUCTS_H_

#include <zephyr/toolchain.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/sys/util.h>
#include <zephyr/kernel_stats.h>
#include <zephyr/kernel/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

struct z_kernel {
    struct k_thread *current_thread;
    struct _ready_q ready_q;
    struct _timeout *timeout_list;
    unsigned int nested_interrupts;
};

extern struct z_kernel _kernel;

static inline struct k_thread *_current_get(void)
{
    return _kernel.current_thread;
}

#define _current _current_get()

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_KERNEL_INCLUDE_KERNEL_STRUCTS_H_ */
