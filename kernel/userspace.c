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

#include <zephyr/kernel.h>
#include <zephyr/kernel/userspace.h>
#include <string.h>

/* Static kernel object table (generated at build time) */
extern struct k_object *_static_kernel_objects_begin[];
extern struct k_object *_static_kernel_objects_end[];

/* Dynamic kernel object list */
static sys_dlist_t dynamic_object_list;

/**
 * @brief Initialize the userspace subsystem.
 */
static int userspace_init(void)
{
    sys_dlist_init(&dynamic_object_list);
    return 0;
}

SYS_INIT(userspace_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

/**
 * @brief Find a kernel object.
 *
 * This routine finds the kernel object associated with the given pointer.
 *
 * @param obj Pointer to the kernel object.
 * @return Pointer to the kernel object metadata, or NULL if not found.
 */
struct k_object *k_object_find(const void *obj)
{
    struct k_object *ko;

    /* Search static objects */
    for (ko = *_static_kernel_objects_begin;
         ko < *_static_kernel_objects_end;
         ko++) {
        if (ko->name == obj) {
            return ko;
        }
    }

    /* Search dynamic objects */
    SYS_DLIST_FOR_EACH_NODE(&dynamic_object_list, node) {
        ko = CONTAINER_OF(node, struct k_object, name);
        if (ko->name == obj) {
            return ko;
        }
    }

    return NULL;
}

/**
 * @brief Grant access to a kernel object.
 *
 * This routine grants access to the specified kernel object for the
 * specified thread.
 *
 * @param obj Pointer to the kernel object.
 * @param thread Thread to grant access to.
 */
void k_object_access_grant(const void *obj, struct k_thread *thread)
{
    struct k_object *ko = k_object_find(obj);

    if (ko != NULL && thread != NULL) {
        /* Set the permission bit for this thread */
        int idx = thread->base.order_key / 8;
        int bit = thread->base.order_key % 8;

        if (idx < CONFIG_MAX_THREAD_BYTES) {
            ko->perms[idx] |= (1 << bit);
        }
    }
}

/**
 * @brief Revoke access to a kernel object.
 *
 * This routine revokes access to the specified kernel object for the
 * specified thread.
 *
 * @param obj Pointer to the kernel object.
 * @param thread Thread to revoke access from.
 */
void k_object_access_revoke(const void *obj, struct k_thread *thread)
{
    struct k_object *ko = k_object_find(obj);

    if (ko != NULL && thread != NULL) {
        /* Clear the permission bit for this thread */
        int idx = thread->base.order_key / 8;
        int bit = thread->base.order_key % 8;

        if (idx < CONFIG_MAX_THREAD_BYTES) {
            ko->perms[idx] &= ~(1 << bit);
        }
    }
}

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
bool k_object_access_check(const void *obj, struct k_thread *thread)
{
    struct k_object *ko = k_object_find(obj);

    if (ko == NULL) {
        return false;
    }

    /* Public objects are accessible by all threads */
    if (ko->flags & K_OBJ_FLAG_PUBLIC) {
        return true;
    }

    /* Check the permission bit for this thread */
    if (thread != NULL) {
        int idx = thread->base.order_key / 8;
        int bit = thread->base.order_key % 8;

        if (idx < CONFIG_MAX_THREAD_BYTES) {
            return (ko->perms[idx] & (1 << bit)) != 0;
        }
    }

    return false;
}

/**
 * @brief Allocate a kernel object.
 *
 * This routine allocates a kernel object of the specified type.
 *
 * @param type Kernel object type.
 * @return Pointer to the allocated kernel object, or NULL if allocation failed.
 */
void *k_object_alloc(enum k_objects type)
{
    struct k_object *ko;

    ko = k_malloc(sizeof(struct k_object));
    if (ko != NULL) {
        memset(ko, 0, sizeof(struct k_object));
        ko->type = type;
        ko->flags = K_OBJ_FLAG_INITIALIZED;

        /* Add to dynamic object list */
        sys_dlist_append(&dynamic_object_list, &ko->name);
    }

    return ko;
}

/**
 * @brief Release a kernel object.
 *
 * This routine releases the specified kernel object.
 *
 * @param obj Pointer to the kernel object.
 */
void k_object_release(const void *obj)
{
    struct k_object *ko = k_object_find(obj);

    if (ko != NULL) {
        /* Remove from dynamic object list */
        sys_dlist_remove(&ko->name);

        /* Free the memory */
        k_free(ko);
    }
}
