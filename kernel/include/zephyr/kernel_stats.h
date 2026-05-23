/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_KERNEL_STATS_H_
#define ZEPHYR_KERNEL_INCLUDE_KERNEL_STATS_H_

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thread statistics */
struct k_thread_stats {
    uint32_t idle;
    uint32_t usage;
    uint32_t aborted;
};

/* Check if a thread is the idle thread */
static inline bool z_is_idle_thread_object(const struct k_thread *thread)
{
    /* TODO: Implement proper idle thread detection */
    return false;
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_KERNEL_INCLUDE_KERNEL_STATS_H_ */
