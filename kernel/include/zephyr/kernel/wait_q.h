/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_WAIT_Q_H_
#define ZEPHYR_KERNEL_INCLUDE_WAIT_Q_H_

#include <zephyr/toolchain.h>
#include <zephyr/sys/dlist.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Wait queue structure */
struct _wait_q {
    sys_dlist_t waitq;
};

#define Z_WAIT_Q_INIT(wait_q_addr) \
    { .waitq = SYS_DLIST_STATIC_INIT(&(wait_q_addr)->waitq) }

#define Z_WAIT_Q_DEFINE(name) \
    struct _wait_q name = Z_WAIT_Q_INIT(&name)

static inline void z_waitq_init(struct _wait_q *wait_q)
{
    sys_dlist_init(&wait_q->waitq);
}

static inline bool z_waitq_is_empty(struct _wait_q *wait_q)
{
    return sys_dlist_is_empty(&wait_q->waitq);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_KERNEL_INCLUDE_WAIT_Q_H_ */
