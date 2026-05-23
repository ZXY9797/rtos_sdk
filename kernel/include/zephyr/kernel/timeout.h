/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_TIMEOUT_H_
#define ZEPHYR_KERNEL_INCLUDE_TIMEOUT_H_

#include <zephyr/toolchain.h>
#include <zephyr/sys/dlist.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Tick type */
typedef uint32_t k_ticks_t;

/* Timeout structure */
struct _timeout {
    sys_dnode_t node;
    k_ticks_t ticks;
    void (*fn)(struct _timeout *timeout);
};

/* Timeout initialization */
static inline void z_init_timeout(struct _timeout *timeout)
{
    timeout->fn = NULL;
    timeout->ticks = 0;
}

/* Timeout functions */
extern void z_add_timeout(struct _timeout *timeout, void (*func)(struct _timeout *));
extern void z_remove_timeout(struct _timeout *timeout);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_KERNEL_INCLUDE_TIMEOUT_H_ */
