/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_ATOMIC_BUILTIN_H_
#define ZEPHYR_INCLUDE_SYS_ATOMIC_BUILTIN_H_

#include <stdbool.h>
#include <zephyr/toolchain.h>
#include <zephyr/sys/atomic_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Atomic type */
typedef int atomic_t;
typedef void *atomic_ptr_t;

/* Atomic init macros */
#define ATOMIC_INIT(i) (i)
#define ATOMIC_PTR_INIT(p) (p)

/* Atomic operations using built-in functions */

static inline atomic_val_t atomic_get(const atomic_t *target)
{
    return *target;
}

static inline atomic_val_t atomic_set(atomic_t *target, atomic_val_t value)
{
    return __atomic_exchange_n(target, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_add(atomic_t *target, atomic_val_t value)
{
    return __atomic_fetch_add(target, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_sub(atomic_t *target, atomic_val_t value)
{
    return __atomic_fetch_sub(target, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_inc(atomic_t *target)
{
    return atomic_add(target, 1);
}

static inline atomic_val_t atomic_dec(atomic_t *target)
{
    return atomic_sub(target, 1);
}

static inline atomic_val_t atomic_or(atomic_t *target, atomic_val_t value)
{
    return __atomic_fetch_or(target, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_and(atomic_t *target, atomic_val_t value)
{
    return __atomic_fetch_and(target, value, __ATOMIC_SEQ_CST);
}

static inline atomic_val_t atomic_xor(atomic_t *target, atomic_val_t value)
{
    return __atomic_fetch_xor(target, value, __ATOMIC_SEQ_CST);
}

static inline bool atomic_cas(atomic_t *target, atomic_val_t old_value,
                              atomic_val_t new_value)
{
    atomic_val_t expected = old_value;
    return __atomic_compare_exchange_n(target, &expected, new_value,
                                       false, __ATOMIC_SEQ_CST,
                                       __ATOMIC_SEQ_CST);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_ATOMIC_BUILTIN_H_ */
