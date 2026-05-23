/*
 * Copyright (c) 2010-2014 Wind River Systems, Inc.
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <string.h>

void *k_heap_alloc(struct k_heap *heap, size_t bytes, k_timeout_t timeout)
{
	ARG_UNUSED(heap);
	ARG_UNUSED(bytes);
	ARG_UNUSED(timeout);
	return NULL;
}

void k_heap_free(struct k_heap *heap, void *mem)
{
	ARG_UNUSED(heap);
	ARG_UNUSED(mem);
}

void k_heap_init(struct k_heap *heap, void *mem, size_t bytes)
{
	ARG_UNUSED(heap);
	ARG_UNUSED(mem);
	ARG_UNUSED(bytes);
}
