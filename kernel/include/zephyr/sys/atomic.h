/*
 * Copyright (c) 1997-2015, Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_ATOMIC_H_
#define ZEPHYR_INCLUDE_SYS_ATOMIC_H_

#include <stdbool.h>
#include <zephyr/toolchain.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Atomic type definitions */
typedef int atomic_t;
typedef void *atomic_ptr_t;

/* Atomic value type */
typedef int atomic_val_t;

/* Atomic init macros */
#define ATOMIC_INIT(i) (i)
#define ATOMIC_PTR_INIT(p) (p)

/* Atomic bits macros */
#define ATOMIC_BITS            (sizeof(atomic_val_t) * BITS_PER_BYTE)
#define ATOMIC_MASK(bit) BIT((unsigned long)(bit) & (ATOMIC_BITS - 1U))
#define ATOMIC_ELEM(addr, bit) ((addr) + ((bit) / ATOMIC_BITS))
#define ATOMIC_BITMAP_SIZE(num_bits) (ROUND_UP(num_bits, ATOMIC_BITS) / ATOMIC_BITS)

/**
 * @brief Atomic operations
 * @{
 */

/**
 * @brief Atomic get.
 *
 * This routine performs an atomic read of @a target.
 *
 * @param target Address of atomic variable.
 *
 * @return Value of @a target.
 */
static inline atomic_val_t atomic_get(const atomic_t *target)
{
    return *target;
}

/**
 * @brief Atomic set.
 *
 * This routine performs an atomic write of @a value to @a target.
 *
 * @param target Address of atomic variable.
 * @param value Value to write.
 *
 * @return Previous value of @a target.
 */
static inline atomic_val_t atomic_set(atomic_t *target, atomic_val_t value)
{
    atomic_val_t old = *target;
    *target = value;
    return old;
}

/**
 * @brief Atomic add.
 *
 * This routine performs an atomic addition of @a value to @a target.
 *
 * @param target Address of atomic variable.
 * @param value Value to add.
 *
 * @return Previous value of @a target.
 */
static inline atomic_val_t atomic_add(atomic_t *target, atomic_val_t value)
{
    atomic_val_t old = *target;
    *target += value;
    return old;
}

/**
 * @brief Atomic subtract.
 *
 * This routine performs an atomic subtraction of @a value from @a target.
 *
 * @param target Address of atomic variable.
 * @param value Value to subtract.
 *
 * @return Previous value of @a target.
 */
static inline atomic_val_t atomic_sub(atomic_t *target, atomic_val_t value)
{
    atomic_val_t old = *target;
    *target -= value;
    return old;
}

/**
 * @brief Atomic increment.
 *
 * This routine performs an atomic increment of @a target by 1.
 *
 * @param target Address of atomic variable.
 *
 * @return Previous value of @a target.
 */
static inline atomic_val_t atomic_inc(atomic_t *target)
{
    return atomic_add(target, 1);
}

/**
 * @brief Atomic decrement.
 *
 * This routine performs an atomic decrement of @a target by 1.
 *
 * @param target Address of atomic variable.
 *
 * @return Previous value of @a target.
 */
static inline atomic_val_t atomic_dec(atomic_t *target)
{
    return atomic_sub(target, 1);
}

/**
 * @brief Atomic OR.
 *
 * This routine performs an atomic bitwise OR of @a value with @a target.
 *
 * @param target Address of atomic variable.
 * @param value Value to OR.
 *
 * @return Previous value of @a target.
 */
static inline atomic_val_t atomic_or(atomic_t *target, atomic_val_t value)
{
    atomic_val_t old = *target;
    *target |= value;
    return old;
}

/**
 * @brief Atomic AND.
 *
 * This routine performs an atomic bitwise AND of @a value with @a target.
 *
 * @param target Address of atomic variable.
 * @param value Value to AND.
 *
 * @return Previous value of @a target.
 */
static inline atomic_val_t atomic_and(atomic_t *target, atomic_val_t value)
{
    atomic_val_t old = *target;
    *target &= value;
    return old;
}

/**
 * @brief Atomic XOR.
 *
 * This routine performs an atomic bitwise XOR of @a value with @a target.
 *
 * @param target Address of atomic variable.
 * @param value Value to XOR.
 *
 * @return Previous value of @a target.
 */
static inline atomic_val_t atomic_xor(atomic_t *target, atomic_val_t value)
{
    atomic_val_t old = *target;
    *target ^= value;
    return old;
}

/**
 * @brief Atomic compare-and-swap.
 *
 * This routine performs an atomic compare-and-swap on @a target.
 *
 * @param target Address of atomic variable.
 * @param old_value Expected old value.
 * @param new_value New value to store.
 *
 * @return true if the value was updated, false otherwise.
 */
static inline bool atomic_cas(atomic_t *target, atomic_val_t old_value,
                              atomic_val_t new_value)
{
    if (*target == old_value) {
        *target = new_value;
        return true;
    }
    return false;
}

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_ATOMIC_H_ */
