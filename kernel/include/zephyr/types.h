/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_TYPES_H_
#define ZEPHYR_INCLUDE_TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type definitions */
typedef int bool_t;

/* Signed type definitions */
typedef int8_t s8_t;
typedef int16_t s16_t;
typedef int32_t s32_t;
typedef int64_t s64_t;

/* Unsigned type definitions */
typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;
typedef uint64_t u64_t;

/* Volatile type definitions */
typedef volatile s8_t vs8_t;
typedef volatile s16_t vs16_t;
typedef volatile s32_t vs32_t;
typedef volatile s64_t vs64_t;

typedef volatile u8_t vu8_t;
typedef volatile u16_t vu16_t;
typedef volatile u32_t vu32_t;
typedef volatile u64_t vu64_t;

/* Memory size type */
typedef size_t mem_size_t;

/* Pointer type */
typedef uintptr_t uintptr_t;
typedef intptr_t intptr_t;

/* Atomic type */
typedef int atomic_t;
typedef void *atomic_ptr_t;

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_TYPES_H_ */
