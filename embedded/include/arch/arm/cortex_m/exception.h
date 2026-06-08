#ifndef ZEPHYR_INCLUDE_ARCH_ARM_CORTEX_M_EXCEPTION_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_CORTEX_M_EXCEPTION_H_
#include <devicetree.h>

#include <arch/arm/cortex_m/nvic.h>

/* for assembler, only works with constants */
#define Z_EXC_PRIO(pri) (((pri) << (8 - NUM_IRQ_PRIO_BITS)) & 0xff)

/*
 * In architecture variants with non-programmable fault exceptions
 * (e.g. Cortex-M Baseline variants), hardware ensures processor faults
 * are given the highest interrupt priority level. SVCalls are assigned
 * the highest configurable priority level (level 0); note, however, that
 * this interrupt level may be shared with HW interrupts.
 *
 * In Cortex variants with programmable fault exception priorities we
 * assign the highest interrupt priority level (level 0) to processor faults
 * with configurable priority.
 * The highest priority level may be shared with either Zero-Latency IRQs (if
 * support for the feature is enabled) or with SVCall priority level.
 * Regular HW IRQs are always assigned priority levels lower than the priority
 * levels for SVCalls, Zero-Latency IRQs and processor faults.
 *
 * PendSV IRQ (which is used in Cortex-M variants to implement thread
 * context-switching) is assigned the lowest IRQ priority level.
 */
#if defined(CONFIG_CPU_CORTEX_M_HAS_PROGRAMMABLE_FAULT_PRIOS)
#define _EXCEPTION_RESERVED_PRIO 1
#else
#define _EXCEPTION_RESERVED_PRIO 0
#endif

#define _EXC_FAULT_PRIO             0
#define _EXC_ZERO_LATENCY_IRQS_PRIO 0
#define _EXC_SVC_PRIO               COND_CODE_1(CONFIG_ZERO_LATENCY_IRQS,		\
				  (CONFIG_ZERO_LATENCY_LEVELS), (0))
#define _IRQ_PRIO_OFFSET            (_EXCEPTION_RESERVED_PRIO + _EXC_SVC_PRIO)
#define IRQ_PRIO_LOWEST             (BIT(NUM_IRQ_PRIO_BITS) - (_IRQ_PRIO_OFFSET) - 1)

#define _EXC_IRQ_DEFAULT_PRIO Z_EXC_PRIO(_IRQ_PRIO_OFFSET)

/* Use lowest possible priority level for PendSV */
#define _EXC_PENDSV_PRIO      0xff
#define _EXC_PENDSV_PRIO_MASK Z_EXC_PRIO(_EXC_PENDSV_PRIO)

#endif
