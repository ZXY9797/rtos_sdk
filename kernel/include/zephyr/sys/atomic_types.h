/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_SYS_ATOMIC_TYPES_H_
#define ZEPHYR_INCLUDE_SYS_ATOMIC_TYPES_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Atomic value type */
typedef int atomic_val_t;

/* Atomic pointer value type */
typedef void *atomic_ptr_val_t;

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_ATOMIC_TYPES_H_ */
