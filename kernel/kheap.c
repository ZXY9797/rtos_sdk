/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <string.h>

#if (CONFIG_HEAP_MEM_POOL_SIZE > 0)
K_HEAP_DEFINE(_system_heap, CONFIG_HEAP_MEM_POOL_SIZE);
#define _SYSTEM_HEAP (&_system_heap)
#else
#define _SYSTEM_HEAP NULL
#endif

void *k_malloc(size_t bytes)
{
	ARG_UNUSED(bytes);
	return NULL;
}

void k_free(void *mem)
{
	ARG_UNUSED(mem);
}

void *k_calloc(size_t nmemb, size_t size)
{
	ARG_UNUSED(nmemb);
	ARG_UNUSED(size);
	return NULL;
}
