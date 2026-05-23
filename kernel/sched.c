/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <string.h>

void z_sched_init(void)
{
}

void z_ready_thread(struct k_thread *thread)
{
	if (thread != NULL) {
		thread->base.thread_state = _THREAD_QUEUED;
	}
}

void z_unready_thread(struct k_thread *thread)
{
	if (thread != NULL) {
		thread->base.thread_state = _THREAD_PENDING;
	}
}

void z_move_thread_to_end_of_prio_q(struct k_thread *thread)
{
	ARG_UNUSED(thread);
}

void z_yield(void)
{
}

void z_schedule_new_thread(struct k_thread *new_thread)
{
	z_ready_thread(new_thread);
}
