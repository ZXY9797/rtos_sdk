/*
 * Copyright (c) 2017 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Memory domain management
 *
 * This module provides memory domain management services for the kernel.
 */

#include <zephyr/kernel.h>
#include <string.h>

/* Default memory domain */
struct k_mem_domain k_mem_domain_default;

/* Memory domain lock */
static struct k_spinlock z_mem_domain_lock;

/**
 * @brief Initialize the memory domain subsystem.
 */
static int mem_domain_init(void)
{
    /* Initialize the default memory domain */
    memset(&k_mem_domain_default, 0, sizeof(k_mem_domain_default));

    return 0;
}

SYS_INIT(mem_domain_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

/**
 * @brief Initialize a memory domain.
 *
 * This routine initializes a memory domain.
 *
 * @param domain Memory domain to initialize.
 * @param num_parts Number of initial partitions.
 * @param parts Array of initial partitions.
 *
 * @return 0 on success, negative errno on failure.
 */
int k_mem_domain_init(struct k_mem_domain *domain, uint8_t num_parts,
                      struct k_mem_partition *parts[])
{
    __ASSERT(domain != NULL, "domain cannot be NULL");
    __ASSERT(num_parts <= CONFIG_MAX_DOMAIN_PARTITIONS, "too many partitions");

    k_spinlock_key_t key = k_spin_lock(&z_mem_domain_lock);

    memset(domain, 0, sizeof(*domain));

    /* Add initial partitions */
    for (int i = 0; i < num_parts; i++) {
        if (parts[i] != NULL) {
            domain->partitions[i] = *parts[i];
            domain->num_partitions++;
        }
    }

    k_spin_unlock(&z_mem_domain_lock, key);

    return 0;
}

/**
 * @brief Add a partition to a memory domain.
 *
 * This routine adds a partition to a memory domain.
 *
 * @param domain Memory domain to add partition to.
 * @param part Partition to add.
 *
 * @return 0 on success, negative errno on failure.
 */
int k_mem_domain_add_partition(struct k_mem_domain *domain,
                               struct k_mem_partition *part)
{
    __ASSERT(domain != NULL, "domain cannot be NULL");
    __ASSERT(part != NULL, "part cannot be NULL");

    k_spinlock_key_t key = k_spin_lock(&z_mem_domain_lock);

    /* Find a free slot */
    int slot = -1;
    for (int i = 0; i < CONFIG_MAX_DOMAIN_PARTITIONS; i++) {
        if (domain->partitions[i].size == 0) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        k_spin_unlock(&z_mem_domain_lock, key);
        return -ENOSPC;
    }

    /* Add the partition */
    domain->partitions[slot] = *part;
    domain->num_partitions++;

    k_spin_unlock(&z_mem_domain_lock, key);

    return 0;
}

/**
 * @brief Remove a partition from a memory domain.
 *
 * This routine removes a partition from a memory domain.
 *
 * @param domain Memory domain to remove partition from.
 * @param part Partition to remove.
 *
 * @return 0 on success, negative errno on failure.
 */
int k_mem_domain_remove_partition(struct k_mem_domain *domain,
                                  struct k_mem_partition *part)
{
    __ASSERT(domain != NULL, "domain cannot be NULL");
    __ASSERT(part != NULL, "part cannot be NULL");

    k_spinlock_key_t key = k_spin_lock(&z_mem_domain_lock);

    /* Find the partition */
    int slot = -1;
    for (int i = 0; i < CONFIG_MAX_DOMAIN_PARTITIONS; i++) {
        if (domain->partitions[i].start == part->start &&
            domain->partitions[i].size == part->size) {
            slot = i;
            break;
        }
    }

    if (slot < 0) {
        k_spin_unlock(&z_mem_domain_lock, key);
        return -EINVAL;
    }

    /* Remove the partition */
    memset(&domain->partitions[slot], 0, sizeof(domain->partitions[slot]));
    domain->num_partitions--;

    k_spin_unlock(&z_mem_domain_lock, key);

    return 0;
}

/**
 * @brief Add a thread to a memory domain.
 *
 * This routine adds a thread to a memory domain.
 *
 * @param domain Memory domain to add thread to.
 * @param thread Thread to add.
 *
 * @return 0 on success, negative errno on failure.
 */
int k_mem_domain_add_thread(struct k_mem_domain *domain,
                            struct k_thread *thread)
{
    __ASSERT(domain != NULL, "domain cannot be NULL");
    __ASSERT(thread != NULL, "thread cannot be NULL");

    k_spinlock_key_t key = k_spin_lock(&z_mem_domain_lock);

    /* Remove from old domain if needed */
    if (thread->mem_domain_info.mem_domain != NULL) {
        /* Remove from old domain */
    }

    /* Add to new domain */
    thread->mem_domain_info.mem_domain = domain;

    k_spin_unlock(&z_mem_domain_lock, key);

    return 0;
}

/**
 * @brief Remove a thread from a memory domain.
 *
 * This routine removes a thread from its current memory domain.
 *
 * @param thread Thread to remove.
 *
 * @return 0 on success, negative errno on failure.
 */
int k_mem_domain_remove_thread(struct k_thread *thread)
{
    __ASSERT(thread != NULL, "thread cannot be NULL");

    k_spinlock_key_t key = k_spin_lock(&z_mem_domain_lock);

    /* Remove from domain */
    thread->mem_domain_info.mem_domain = NULL;

    k_spin_unlock(&z_mem_domain_lock, key);

    return 0;
}
