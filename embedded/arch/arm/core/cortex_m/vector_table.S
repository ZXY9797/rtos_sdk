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

_ASM_FILE_PROLOGUE
.eabi_attribute Tag_ABI_align_preserved, 1

.global  Default_Handler

    .section  .text.Default_Handler,"ax",%progbits
Default_Handler:
Infinite_Loop:
  b  Infinite_Loop
  .size  Default_Handler, .-Default_Handler

/*
 * Macros for generating IRQ entries based on CONFIG_NUM_IRQS.
 * Uses .altmacro mode so %expr evaluates to the numeric value.
 */
.altmacro

.macro _irq_entry i
    .word IRQ\i\()_Handler
.endm

.macro _irq_weak i
    .weak      IRQ\i\()_Handler
    .thumb_set IRQ\i\()_Handler, Default_Handler
.endm

.macro gen_irq_entries count:req
    .set _irq_i, 0
    .rept \count
    _irq_entry %_irq_i
    .set _irq_i, _irq_i + 1
    .endr
.endm

.macro gen_irq_Weaks count:req
    .set _irq_i, 0
    .rept \count
    _irq_weak %_irq_i
    .set _irq_i, _irq_i + 1
    .endr
.endm

@ #define SECTION_SUBSEC_FUNC(sect, subsec, sym)
@ 		.section .sect.subsec, "ax"; PERFOPT_ALIGN; sym :

SECTION_SUBSEC_FUNC(exc_vector_table,_vector_table_section,_vector_table)

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

    /* IRQ entries — generated from CONFIG_NUM_IRQS */
    gen_irq_entries CONFIG_NUM_IRQS

    /* System exception weak aliases */
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

   /* IRQ weak aliases — generated from CONFIG_NUM_IRQS */
   gen_irq_Weaks CONFIG_NUM_IRQS
