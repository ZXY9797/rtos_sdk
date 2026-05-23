/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>

static volatile uint32_t sys_tick_count;

void z_timeout_init(void)
{
	sys_tick_count = 0;
}

void z_add_timeout(struct _timeout *timeout, void (*func)(struct _timeout *))
{
	ARG_UNUSED(timeout);
	ARG_UNUSED(func);
}

void z_remove_timeout(struct _timeout *timeout)
{
	ARG_UNUSED(timeout);
}

k_ticks_t k_tick_get(void)
{
	return sys_tick_count;
}

int64_t k_uptime_get(void)
{
	return (int64_t)sys_tick_count * 1000 / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
}

/* Note: K_MSEC, K_SEC, K_USEC macros in kernel.h use CONFIG_SYS_CLOCK_TICKS_PER_SEC directly */

/* Called by timer drivers to announce ticks */
void sys_clock_announce(int32_t ticks)
{
	sys_tick_count += ticks;
}
