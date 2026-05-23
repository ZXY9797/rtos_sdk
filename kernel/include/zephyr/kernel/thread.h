/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_THREAD_H_
#define ZEPHYR_KERNEL_INCLUDE_THREAD_H_

#include <zephyr/toolchain.h>
#include <zephyr/sys/dlist.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thread entry point type */
typedef void (*k_thread_entry_t)(void *p1, void *p2, void *p3);

/* Thread stack type */
typedef struct z_thread_stack_element {
    uint8_t data;
} k_thread_stack_t;

/* Thread ID type */
typedef struct k_thread *k_tid_t;

/* Thread stack definition macros */
#define K_THREAD_STACK_DEFINE(sym, size) \
    Z_DECL_ALIGN(k_thread_stack_t) sym[DIV_ROUND_UP(size, sizeof(k_thread_stack_t))]

#define K_THREAD_STACK_SIZEOF(sym) (sizeof(sym))

/* Thread state values */
#define _THREAD_DUMMY           0
#define _THREAD_PENDING         1
#define _THREAD_PRESTART        2
#define _THREAD_DEAD            3
#define _THREAD_SUSPENDED       4
#define _THREAD_ABORTING        5
#define _THREAD_QUEUED          6

/* Queue node for ready queue */
struct _ready_q_node {
    sys_dnode_t node;
};

/* Queue node for wait queue */
struct _wait_q_node {
    sys_dnode_t node;
};

/* Thread base structure */
struct _thread_base {
    union {
        struct _ready_q_node qnode;
        struct _wait_q_node wnode;
    };

    uint32_t order_key;

    uint8_t user_options;
    uint8_t thread_state;
    int8_t prio;
};

/* Thread structure */
struct k_thread {
    struct _thread_base base;
    k_thread_entry_t entry;
    struct _thread_arch arch;
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_KERNEL_INCLUDE_THREAD_H_ */
