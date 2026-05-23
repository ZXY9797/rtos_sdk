/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/kernel_structs.h>
#include <string.h>

static uint32_t thread_counter;

k_tid_t k_thread_create(struct k_thread *new_thread,
			k_thread_stack_t *stack,
			size_t stack_size,
			k_thread_entry_t entry,
			void *p1, void *p2, void *p3,
			int prio, uint32_t options,
			k_timeout_t delay)
{
	ARG_UNUSED(stack_size);
	ARG_UNUSED(delay);

	if (new_thread == NULL || stack == NULL || entry == NULL) {
		return NULL;
	}

	memset(new_thread, 0, sizeof(struct k_thread));

	new_thread->entry = entry;
	new_thread->base.prio = prio;
	new_thread->base.user_options = options;
	new_thread->base.thread_state = _THREAD_PRESTART;
	new_thread->base.order_key = thread_counter++;

	return (k_tid_t)new_thread;
}

int k_thread_start(k_tid_t thread)
{
	struct k_thread *t = (struct k_thread *)thread;

	if (t == NULL) {
		return -EINVAL;
	}

	t->base.thread_state = _THREAD_QUEUED;

	return 0;
}

void k_thread_abort(k_tid_t thread)
{
	struct k_thread *t = (struct k_thread *)thread;

	if (t == NULL) {
		return;
	}

	t->base.thread_state = _THREAD_DEAD;
}

int32_t k_sleep(k_timeout_t duration)
{
	ARG_UNUSED(duration);
	return 0;
}

void k_yield(void)
{
}

k_tid_t k_current_get(void)
{
	return (k_tid_t)_current_get();
}

int k_thread_priority_get(k_tid_t thread)
{
	struct k_thread *t = (struct k_thread *)thread;

	if (t == NULL) {
		return 0;
	}

	return t->base.prio;
}

void k_thread_priority_set(k_tid_t thread, int prio)
{
	struct k_thread *t = (struct k_thread *)thread;

	if (t == NULL) {
		return;
	}

	t->base.prio = prio;
}
