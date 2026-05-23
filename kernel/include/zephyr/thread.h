/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Thread data structure definitions.
 */

#ifndef ZEPHYR_KERNEL_INCLUDE_THREAD_H_
#define ZEPHYR_KERNEL_INCLUDE_THREAD_H_

#include <zephyr/toolchain.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/sys/util.h>
#include <zephyr/arch/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Thread state values */
#define _THREAD_DUMMY           0
#define _THREAD_PENDING         1
#define _THREAD_PRESTART        2
#define _THREAD_DEAD            3
#define _THREAD_SUSPENDED       4
#define _THREAD_ABORTING        5
#define _THREAD_QUEUED          6

/* Thread user options */

/**
 * Thread Stack
 *
 * Stack data structure and helper macros.
 */

/* Stack buffer definition */
struct z_thread_stack_element {
    uint8_t data;
};

typedef struct z_thread_stack_element k_thread_stack_t;

/* Stack size macros */
#define Z_THREAD_STACK_DEFINE_IN(sym, size, lsection) \
    lsection Z_DECL_ALIGN(k_thread_stack_t) sym[ \
        DIV_ROUND_UP(size, sizeof(k_thread_stack_t))]

#define Z_THREAD_STACK_DEFINE(sym, size) \
    Z_THREAD_STACK_DEFINE_IN(sym, size, __noinit)

#define Z_THREAD_STACK_SIZEOF(sym) (sizeof(sym))

/**
 * @brief Thread structure
 *
 * This structure contains all data related to a thread.
 */
struct k_thread {
    /* Thread base data */
    struct _thread_base base;

    /* Architecture-specific callee-saved registers */
    struct _callee_saved callee_saved;

    /* Thread entry point and parameters */
    k_thread_entry_t entry;
    void *init_data[3];

    /* Thread stack information */
    struct _thread_stack_info stack_info;

    /* Thread name */
#if defined(CONFIG_THREAD_NAME)
    char name[CONFIG_THREAD_MAX_NAME_LEN];
#endif

    /* Thread join queue */
    sys_dlist_t join_queue;

    /* Swap return value */
    int swap_retval;

    /* Architecture-specific data */
    struct _thread_arch arch;

    /* Userspace data */
#if defined(CONFIG_USERSPACE)
    struct _mem_domain_info mem_domain_info;
    k_thread_stack_t *stack_obj;
    void *syscall_frame;
#endif

    /* Thread local storage */
#if defined(CONFIG_THREAD_LOCAL_STORAGE)
    void *tls;
#endif
};

/**
 * @brief Stack information structure
 */
struct _thread_stack_info {
    size_t size;
    size_t start;
    size_t delta;
};

/**
 * @brief Thread base structure
 */
struct _thread_base {
    union {
        struct _ready_q_node qnode;
        struct _wait_q_node wnode;
    };

    uint32_t order_key;

#if defined(CONFIG_SMP)
    uint8_t cpu_mask;
    uint8_t cpu;
#endif

    uint8_t user_options;
    uint8_t thread_state;
    int8_t prio;
};

/**
 * @brief Ready queue node
 */
struct _ready_q_node {
    sys_dnode_t node;
};

/**
 * @brief Wait queue node
 */
struct _wait_q_node {
    sys_dnode_t node;
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_KERNEL_INCLUDE_THREAD_H_ */
