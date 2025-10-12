/*
 * Copyright (c) 2013-2015 Wind River Systems, Inc.
 * Copyright (c) 2020 Nordic Semiconductor ASA.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Populated vector table in ROM
 *
 * Vector table at the beginning of the image for starting system. The reset
 * vector is the system entry point, ie. the first instruction executed.
 *
 * The table is populated with all the system exception handlers. The NMI vector
 * must be populated with a valid handler since it can happen at any time. The
 * rest should not be triggered until the kernel is ready to handle them.
 */

#include <toolchain.h>
#include <linker/sections.h>
#include "vector_table.h"

_ASM_FILE_PROLOGUE
/*
 * Tell armclang that stack alignment are ensured.
 */
.eabi_attribute Tag_ABI_align_preserved, 1

.global  Default_Handler
.global _isr_wrapper

    .section  .text.Default_Handler,"ax",%progbits
Default_Handler:
Infinite_Loop:
  b  Infinite_Loop
  .size  Default_Handler, .-Default_Handler

    .section  .text._isr_default_handler,"ax",%progbits
_isr_default_handler:
  b  _isr_wrapper
  .size  _isr_default_handler, .-_isr_default_handler

@ #define SECTION_SUBSEC_FUNC(sect, subsec, sym)
@ 		.section .sect.subsec, "ax"; PERFOPT_ALIGN; sym :

SECTION_SUBSEC_FUNC(exc_vector_table,_vector_table_section,_vector_table)

    /*
     * setting the _very_ early boot on the main stack allows to use memset
     * on the interrupt stack when CONFIG_INIT_STACKS is enabled before
     * switching to the interrupt stack for the rest of the early boot
     */
    .word _estack

    .word Reset_Handler
    .word NMI_Handler

    .word HardFault_Handler
#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word 0
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
    .word MemManage_Handler
    .word BusFault_Handler
    .word UsageFault_Handler
#if defined(CONFIG_ARMV8_M_SE)
#if defined(CONFIG_ARM_SECURE_FIRMWARE)
    .word Default_Handler
#else
    .word Default_Handler
#endif /* CONFIG_ARM_SECURE_FIRMWARE */
#else
    .word 0
#endif /* CONFIG_ARMV8_M_SE */
    .word 0
    .word 0
    .word 0
    .word SVC_Handler
    .word DebugMon_Handler
#else
#error Unknown ARM architecture
#endif /* CONFIG_ARMV6_M_ARMV8_M_BASELINE */
    .word 0
    .word PendSV_Handler
#if defined(CONFIG_CPU_CORTEX_M_HAS_SYSTICK)
    .word SysTick_Handler
#else
    .word 0
#endif /* CONFIG_CPU_CORTEX_M_HAS_SYSTICK */
    .word IRQ0_Handler
    .word IRQ1_Handler
    .word IRQ2_Handler
    .word IRQ3_Handler
    .word IRQ4_Handler
    .word IRQ5_Handler
    .word IRQ6_Handler
    .word IRQ7_Handler
    .word IRQ8_Handler
    .word IRQ9_Handler
    .word IRQ10_Handler
    .word IRQ11_Handler
    .word IRQ12_Handler
    .word IRQ13_Handler
    .word IRQ14_Handler
    .word IRQ15_Handler
    .word IRQ16_Handler
    .word IRQ17_Handler
    .word IRQ18_Handler
    .word IRQ19_Handler
    .word IRQ20_Handler
    .word IRQ21_Handler
    .word IRQ22_Handler
    .word IRQ23_Handler
    .word IRQ24_Handler
    .word IRQ25_Handler
    .word IRQ26_Handler
    .word IRQ27_Handler
    .word IRQ28_Handler
    .word IRQ29_Handler
    .word IRQ30_Handler
    .word IRQ31_Handler
    .word IRQ32_Handler
    .word IRQ33_Handler
    .word IRQ34_Handler
    .word IRQ35_Handler
    .word IRQ36_Handler
    .word IRQ37_Handler
    .word IRQ38_Handler
    .word IRQ39_Handler
    .word IRQ40_Handler
    .word IRQ41_Handler

   .weak      NMI_Handler
   .thumb_set NMI_Handler,Default_Handler

   .weak      HardFault_Handler
   .thumb_set HardFault_Handler,Default_Handler

   .weak      MemManage_Handler
   .thumb_set MemManage_Handler,Default_Handler

   .weak      BusFault_Handler
   .thumb_set BusFault_Handler,Default_Handler

   .weak      UsageFault_Handler
   .thumb_set UsageFault_Handler,Default_Handler

   .weak      SVC_Handler
   .thumb_set SVC_Handler,Default_Handler

   .weak      DebugMon_Handler
   .thumb_set DebugMon_Handler,Default_Handler

   .weak      PendSV_Handler
   .thumb_set PendSV_Handler,Default_Handler

   .weak      SysTick_Handler
   .thumb_set SysTick_Handler,Default_Handler

   .weak      IRQ0_Handler
   .thumb_set IRQ0_Handler,_isr_default_handler

   .weak      IRQ1_Handler
   .thumb_set IRQ1_Handler,_isr_default_handler

   .weak      IRQ2_Handler
   .thumb_set IRQ2_Handler,_isr_default_handler

   .weak      IRQ3_Handler
   .thumb_set IRQ3_Handler,_isr_default_handler

   .weak      IRQ4_Handler
   .thumb_set IRQ4_Handler,_isr_default_handler

   .weak      IRQ5_Handler
   .thumb_set IRQ5_Handler,_isr_default_handler

   .weak      IRQ6_Handler
   .thumb_set IRQ6_Handler,_isr_default_handler

   .weak      IRQ7_Handler
   .thumb_set IRQ7_Handler,_isr_default_handler

   .weak      IRQ8_Handler
   .thumb_set IRQ8_Handler,_isr_default_handler

   .weak      IRQ9_Handler
   .thumb_set IRQ9_Handler,_isr_default_handler

   .weak      IRQ10_Handler
   .thumb_set IRQ10_Handler,_isr_default_handler

   .weak      IRQ11_Handler
   .thumb_set IRQ11_Handler,_isr_default_handler

   .weak      IRQ12_Handler
   .thumb_set IRQ12_Handler,_isr_default_handler

   .weak      IRQ13_Handler
   .thumb_set IRQ13_Handler,_isr_default_handler

   .weak      IRQ14_Handler
   .thumb_set IRQ14_Handler,_isr_default_handler

   .weak      IRQ15_Handler
   .thumb_set IRQ15_Handler,_isr_default_handler

   .weak      IRQ16_Handler
   .thumb_set IRQ16_Handler,_isr_default_handler

   .weak      IRQ17_Handler
   .thumb_set IRQ17_Handler,_isr_default_handler

   .weak      IRQ18_Handler
   .thumb_set IRQ18_Handler,_isr_default_handler

   .weak      IRQ19_Handler
   .thumb_set IRQ19_Handler,_isr_default_handler

   .weak      IRQ20_Handler
   .thumb_set IRQ20_Handler,_isr_default_handler

   .weak      IRQ21_Handler
   .thumb_set IRQ21_Handler,_isr_default_handler

   .weak      IRQ22_Handler
   .thumb_set IRQ22_Handler,_isr_default_handler

   .weak      IRQ23_Handler
   .thumb_set IRQ23_Handler,_isr_default_handler

   .weak      IRQ24_Handler
   .thumb_set IRQ24_Handler,_isr_default_handler

   .weak      IRQ25_Handler
   .thumb_set IRQ25_Handler,_isr_default_handler

   .weak      IRQ26_Handler
   .thumb_set IRQ26_Handler,_isr_default_handler

   .weak      IRQ27_Handler
   .thumb_set IRQ27_Handler,_isr_default_handler

   .weak      IRQ28_Handler
   .thumb_set IRQ28_Handler,_isr_default_handler

   .weak      IRQ29_Handler
   .thumb_set IRQ29_Handler,_isr_default_handler

   .weak      IRQ30_Handler
   .thumb_set IRQ30_Handler,_isr_default_handler

   .weak      IRQ31_Handler
   .thumb_set IRQ31_Handler,_isr_default_handler

   .weak      IRQ32_Handler
   .thumb_set IRQ32_Handler,_isr_default_handler

   .weak      IRQ33_Handler
   .thumb_set IRQ33_Handler,_isr_default_handler

   .weak      IRQ34_Handler
   .thumb_set IRQ34_Handler,_isr_default_handler

   .weak      IRQ35_Handler
   .thumb_set IRQ35_Handler,_isr_default_handler

   .weak      IRQ36_Handler
   .thumb_set IRQ36_Handler,_isr_default_handler

   .weak      IRQ37_Handler
   .thumb_set IRQ37_Handler,_isr_default_handler

   .weak      IRQ38_Handler
   .thumb_set IRQ38_Handler,_isr_default_handler

   .weak      IRQ39_Handler
   .thumb_set IRQ39_Handler,_isr_default_handler

   .weak      IRQ40_Handler
   .thumb_set IRQ40_Handler,_isr_default_handler

   .weak      IRQ41_Handler
   .thumb_set IRQ41_Handler,_isr_default_handler
