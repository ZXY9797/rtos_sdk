/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief User space kernel services
 *
 * This module provides user space related services for the kernel.
 */

#ifndef ZEPHYR_KERNEL_USERSPACE_H_
#define ZEPHYR_KERNEL_USERSPACE_H_

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Kernel object types */
enum k_objects {
    K_OBJ_ANY,
    K_OBJ_THREAD,
    K_OBJ_SEM,
    K_OBJ_MUTEX,
    K_OBJ_MSGQ,
    K_OBJ_PIPE,
    K_OBJ_QUEUE,
    K_OBJ_FIFO,
    K_OBJ_LIFO,
    K_OBJ_STACK,
    K_OBJ_MEM_SLAB,
    K_OBJ_TIMER,
    K_OBJ_THREAD_STACK_ELEMENT,
    K_OBJ_NET_SOCKET,
    K_OBJ_SYS_MUTEX,
    K_OBJ_FUTEX,
    K_OBJ_CONDVAR,
    K_OBJ_EVENT,
    K_OBJ_DRIVER,
    K_OBJ_MAX
};

/* Kernel object structure */
struct k_object {
    void *name;
    uint8_t perms[CONFIG_MAX_THREAD_BYTES];
    uint8_t type;
    uint8_t flags;
    union k_object_data data;
} __packed __aligned(4);

/* Kernel object data union */
union k_object_data {
    struct k_mutex *mutex;
    unsigned int thread_id;
    size_t stack_size;
};

/* Kernel object flags */
#define K_OBJ_FLAG_INITIALIZED (1 << 0)
#define K_OBJ_FLAG_PUBLIC (1 << 1)

/**
 * @brief Find a kernel object.
 *
 * This routine finds the kernel object associated with the given pointer.
 *
 * @param obj Pointer to the kernel object.
 * @return Pointer to the kernel object metadata, or NULL if not found.
 */
struct k_object *k_object_find(const void *obj);

/**
 * @brief Grant access to a kernel object.
 *
 * This routine grants access to the specified kernel object for the
 * specified thread.
 *
 * @param obj Pointer to the kernel object.
 * @param thread Thread to grant access to.
 */
void k_object_access_grant(const void *obj, struct k_thread *thread);

/**
 * @brief Revoke access to a kernel object.
 *
 * This routine revokes access to the specified kernel object for the
 * specified thread.
 *
 * @param obj Pointer to the kernel object.
 * @param thread Thread to revoke access from.
 */
void k_object_access_revoke(const void *obj, struct k_thread *thread);

/**
 * @brief Check if a thread has access to a kernel object.
 *
 * This routine checks if the specified thread has access to the specified
 * kernel object.
 *
 * @param obj Pointer to the kernel object.
 * @param thread Thread to check access for.
 * @return true if the thread has access, false otherwise.
 */
bool k_object_access_check(const void *obj, struct k_thread *thread);

/**
 * @brief Allocate a kernel object.
 *
 * This routine allocates a kernel object of the specified type.
 *
 * @param type Kernel object type.
 * @return Pointer to the allocated kernel object, or NULL if allocation failed.
 */
void *k_object_alloc(enum k_objects type);

/**
 * @brief Release a kernel object.
 *
 * This routine releases the specified kernel object.
 *
 * @param obj Pointer to the kernel object.
 */
void k_object_release(const void *obj);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_KERNEL_USERSPACE_H_ */
