#include <mmem.h>
#include <zephyr/kernel.h>
#include <string.h>

/* Simplified memory management using k_malloc/k_free */
void *mmem_caps_malloc(size_t size, uint32_t caps)
{
    ARG_UNUSED(caps);
    return k_malloc(size);
}

void *mmem_caps_calloc(size_t nitems, size_t size, uint32_t caps)
{
    ARG_UNUSED(caps);
    return k_calloc(nitems, size);
}

void *mmem_malloc(size_t size)
{
    return k_malloc(size);
}

void *mmem_calloc(size_t nitems, size_t size)
{
    return k_calloc(nitems, size);
}

void *mmem_realloc(void *ptr, size_t size)
{
    ARG_UNUSED(ptr);
    ARG_UNUSED(size);
    return NULL;
}

void mmem_free(void *ptr)
{
    k_free(ptr);
}

void *mmem_caps_aligned_alloc(size_t alignment, size_t size, uint32_t caps)
{
    ARG_UNUSED(alignment);
    ARG_UNUSED(caps);
    return k_malloc(size);
}

void mmem_aligned_free(void *ptr)
{
    k_free(ptr);
}
