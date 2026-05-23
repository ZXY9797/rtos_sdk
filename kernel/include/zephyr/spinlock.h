/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SPINLOCK_H_
#define ZEPHYR_INCLUDE_SPINLOCK_H_

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Spinlock structure */
struct k_spinlock {
    unsigned int key;
};

/* Spinlock key type */
typedef struct {
    unsigned int key;
} k_spinlock_key_t;

/* Spinlock functions */
extern k_spinlock_key_t k_spin_lock(struct k_spinlock *l);
extern void k_spin_unlock(struct k_spinlock *l, k_spinlock_key_t key);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SPINLOCK_H_ */
