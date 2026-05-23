/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_SCHED_PRIQ_H_
#define ZEPHYR_KERNEL_INCLUDE_SCHED_PRIQ_H_

#include <zephyr/toolchain.h>
#include <zephyr/sys/dlist.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Priority queue structure */
struct _ready_q {
    sys_dlist_t runq;
};

/* Initialize ready queue */
static inline void z_ready_q_init(struct _ready_q *q)
{
    sys_dlist_init(&q->runq);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_KERNEL_INCLUDE_SCHED_PRIQ_H_ */
