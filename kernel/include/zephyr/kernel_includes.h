/*
 * Copyright (c) 2016, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_KERNEL_INCLUDES_H_
#define ZEPHYR_INCLUDE_KERNEL_INCLUDES_H_

#include <zephyr/toolchain.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/sflist.h>
#include <zephyr/sys_clock.h>

/* Core kernel types first */
#include <zephyr/kernel_stats.h>
#include <zephyr/kernel/timeout.h>
#include <zephyr/kernel/sched_priq.h>

/* Arch-specific headers (defines _thread_arch) */
#include <zephyr/arch/cpu.h>

/* Thread types (needs _thread_arch) */
#include <zephyr/kernel/thread.h>
#include <zephyr/kernel/wait_q.h>

#endif /* ZEPHYR_INCLUDE_KERNEL_INCLUDES_H_ */
