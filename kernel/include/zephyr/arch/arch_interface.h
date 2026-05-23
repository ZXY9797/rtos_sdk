/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Architecture interface
 *
 * This file defines the architecture-independent kernel interfaces.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARCH_INTERFACE_H_
#define ZEPHYR_INCLUDE_ARCH_ARCH_INTERFACE_H_

#include <zephyr/toolchain.h>

/* Forward declarations for types used in this header */
struct k_thread;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Architecture interface functions
 *
 * These functions are implemented by each architecture and declared
 * as static inline in the architecture-specific headers.
 */

/* Fatal error handling */
extern void arch_system_halt(unsigned int reason);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARCH_INTERFACE_H_ */
