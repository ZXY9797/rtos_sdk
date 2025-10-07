#ifndef ZEPHYR_INCLUDE_LINKER_LINKER_DEFS_H_
#define ZEPHYR_INCLUDE_LINKER_LINKER_DEFS_H_
#include <toolchain.h>
#include <sys/util.h>


/* Used by arch_bss_zero or arch-specific implementation */
extern char __bss_start[];
extern char __bss_end[];

/* Used by arch_data_copy() or arch-specific implementation */
#ifdef CONFIG_XIP
extern char __data_region_load_start[];
extern char __data_region_start[];
extern char __data_region_end[];
#endif /* CONFIG_XIP */

extern char _sidata[];
extern char _sdata[];
extern char _edata[];
extern char __ramfunc_region_start[];
extern char __ramfunc_start[];
extern char __ramfunc_end[];
extern char __ramfunc_size[];
extern char __ramfunc_load_start[];

#endif
