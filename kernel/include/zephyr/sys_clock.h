/*
 * Copyright (c) 2014-2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_CLOCK_H_
#define ZEPHYR_INCLUDE_SYS_CLOCK_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t k_ticks_t;

typedef struct {
    k_ticks_t ticks;
} k_timeout_t;

#define Z_TIMEOUT_NO_WAIT ((k_timeout_t) { 0 })
#define Z_TIMEOUT_TICKS(t) ((k_timeout_t) { .ticks = (t) })

static inline bool K_TIMEOUT_EQ(k_timeout_t a, k_timeout_t b)
{
    return a.ticks == b.ticks;
}

k_ticks_t k_tick_get(void);
int64_t k_uptime_get(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_CLOCK_H_ */
