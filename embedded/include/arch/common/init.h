#ifndef ZEPHYR_ARCH_COMMON_INIT_H_
#define ZEPHYR_ARCH_COMMON_INIT_H_
#include <stddef.h>

void z_cstart(void);

/* Early boot functions */
void arch_early_memset(void *dst, int c, size_t n);
void arch_early_memcpy(void *dst, const void *src, size_t n);

void arch_bss_zero(void);

#endif
