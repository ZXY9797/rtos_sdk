/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Wait queue data structure definitions.
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_WAIT_Q_H_
#define ZEPHYR_KERNEL_INCLUDE_WAIT_Q_H_

#include <zephyr/toolchain.h>
#include <zephyr/sys/dlist.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Wait queue structure
 */
struct _wait_q {
    sys_dlist_t waitq;
};

#define Z_WAIT_Q_INIT(wait_q_addr) \
    { .waitq = SYS_DLIST_STATIC_INIT(&(wait_q_addr)->waitq) }

/**
 * @brief Statically define a wait queue.
 *
 * @param name Name of the wait queue.
 */
#define Z_WAIT_Q_DEFINE(name) \
    struct _wait_q name = Z_WAIT_Q_INIT(&name)

/**
 * @brief Initialize a wait queue.
 *
 * @param wait_q Address of the wait queue.
 */
static inline void z_waitq_init(struct _wait_q *wait_q)
{
    sys_dlist_init(&wait_q->waitq);
}

/**
 * @brief Check if a wait queue is empty.
 *
 * @param wait_q Address of the wait queue.
 *
 * @return true if the wait queue is empty, false otherwise.
 */
static inline bool z_waitq_is_empty(struct _wait_q *wait_q)
{
    return sys_dlist_is_empty(&wait_q->waitq);
}

/**
 * @brief Get the first thread in a wait queue.
 *
 * @param wait_q Address of the wait queue.
 *
 * @return Pointer to the first thread, or NULL if empty.
 */
static inline struct k_thread *z_waitq_head(struct _wait_q *wait_q)
{
    sys_dnode_t *node = sys_dlist_peek_head(&wait_q->waitq);

    if (node == NULL) {
        return NULL;
    }

    return CONTAINER_OF(node, struct k_thread, base.wnode.node);
}

/**
 * @brief Add a thread to a wait queue.
 *
 * @param wait_q Address of the wait queue.
 * @param thread Thread to add.
 */
void z_waitq_init(struct _wait_q *wait_q);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_KERNEL_INCLUDE_WAIT_Q_H_ */
