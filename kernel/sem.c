/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <string.h>

int k_sem_init(struct k_sem *sem, unsigned int initial_count, unsigned int limit)
{
	if (sem == NULL) {
		return -EINVAL;
	}

	z_waitq_init(&sem->wait_q);
	sem->count = initial_count;
	sem->limit = limit;

	return 0;
}

int k_sem_take(struct k_sem *sem, k_timeout_t timeout)
{
	if (sem == NULL) {
		return -EINVAL;
	}

	if (sem->count > 0) {
		sem->count--;
		return 0;
	}

	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		return -EBUSY;
	}

	return -EAGAIN;
}

void k_sem_give(struct k_sem *sem)
{
	if (sem == NULL) {
		return;
	}

	if (sem->count >= sem->limit) {
		return;
	}

	sem->count++;
}

unsigned int k_sem_count_get(struct k_sem *sem)
{
	if (sem == NULL) {
		return 0;
	}

	return sem->count;
}
