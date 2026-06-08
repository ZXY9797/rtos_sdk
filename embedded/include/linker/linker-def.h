#ifndef ZEPHYR_INCLUDE_LINKER_LINKER_DEFS_H_
#define ZEPHYR_INCLUDE_LINKER_LINKER_DEFS_H_
#include <toolchain.h>
#include <sys/util.h>
#include <devicetree.h>

/* BSS boundaries — cleared by sct_load() at startup */
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

#if (DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(itcm)))
extern char __itcm_start[];
extern char __itcm_end[];
extern char __itcm_size[];
extern char __itcm_load_start[];
#endif

#if (DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(dtcm)))
extern char __dtcm_data_start[];
extern char __dtcm_data_end[];
extern char __dtcm_bss_start[];
extern char __dtcm_bss_end[];
extern char __dtcm_noinit_start[];
extern char __dtcm_noinit_end[];
extern char __dtcm_data_load_start[];
extern char __dtcm_start[];
extern char __dtcm_end[];
#endif

#endif
