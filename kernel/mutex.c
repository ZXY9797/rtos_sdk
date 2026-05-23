/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <string.h>

int k_mutex_init(struct k_mutex *mutex)
{
	if (mutex == NULL) {
		return -EINVAL;
	}

	z_waitq_init(&mutex->wait_q);
	mutex->owner = NULL;
	mutex->lock_count = 0;
	mutex->owner_orig_prio = 0;

	return 0;
}

int k_mutex_lock(struct k_mutex *mutex, k_timeout_t timeout)
{
	if (mutex == NULL) {
		return -EINVAL;
	}

	if (mutex->owner == NULL) {
		mutex->owner = _current;
		mutex->lock_count = 1;
		mutex->owner_orig_prio = _current ? _current->base.prio : 0;
		return 0;
	}

	if (mutex->owner == _current) {
		mutex->lock_count++;
		return 0;
	}

	if (K_TIMEOUT_EQ(timeout, K_NO_WAIT)) {
		return -EBUSY;
	}

	return -EAGAIN;
}

int k_mutex_unlock(struct k_mutex *mutex)
{
	if (mutex == NULL) {
		return -EINVAL;
	}

	if (mutex->owner != _current) {
		return -EPERM;
	}

	mutex->lock_count--;

	if (mutex->lock_count == 0) {
		mutex->owner = NULL;
	}

	return 0;
}
