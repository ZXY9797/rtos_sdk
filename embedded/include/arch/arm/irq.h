#ifndef ZEPHYR_INCLUDE_ARCH_ARM_IRQ_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_IRQ_H_
#include <sw_isr_table.h>
#include <stdbool.h>

#ifdef _ASMLANGUAGE

#else

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER)
extern void arm_irq_enable(unsigned int irq);
extern void arm_irq_disable(unsigned int irq);
extern int arm_irq_is_enabled(unsigned int irq);
extern void arm_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags);
#if !defined(CONFIG_MULTI_LEVEL_INTERRUPTS)
#define arch_irq_enable(irq)                     arm_irq_enable(irq)
#define arch_irq_disable(irq)                    arm_irq_disable(irq)
#define arch_irq_is_enabled(irq)                 arm_irq_is_enabled(irq)
#define z_arm_irq_priority_set(irq, prio, flags) arm_irq_priority_set(irq, prio, flags)
#endif
#endif

#if defined(CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER) || defined(CONFIG_MULTI_LEVEL_INTERRUPTS)
/*
 * When a custom interrupt controller or multi-level interrupts is specified,
 * map the architecture interrupt control functions to the SoC layer interrupt
 * control functions.
 */

void z_soc_irq_init(void);
void z_soc_irq_enable(unsigned int irq);
void z_soc_irq_disable(unsigned int irq);
int z_soc_irq_is_enabled(unsigned int irq);

void z_soc_irq_priority_set(
	unsigned int irq, unsigned int prio, unsigned int flags);

unsigned int z_soc_irq_get_active(void);
void z_soc_irq_eoi(unsigned int irq);

#define arch_irq_enable(irq)		z_soc_irq_enable(irq)
#define arch_irq_disable(irq)		z_soc_irq_disable(irq)
#define arch_irq_is_enabled(irq)	z_soc_irq_is_enabled(irq)

#define z_arm_irq_priority_set(irq, prio, flags)	\
	z_soc_irq_priority_set(irq, prio, flags)

#endif

/* Flags for use with IRQ_CONNECT() */
/**
 * Set this interrupt up as a zero-latency IRQ. If CONFIG_ZERO_LATENCY_LEVELS
 * is 1 it has a fixed hardware priority level (discarding what was supplied
 * in the interrupt's priority argument). If CONFIG_ZERO_LATENCY_LEVELS is
 * greater 1 it has the priority level assigned by the argument.
 * The interrupt will run even if irq_lock() is active. Be careful!
 */
#define IRQ_ZERO_LATENCY	BIT(0)

#ifdef CONFIG_CPU_CORTEX_M

#if defined(CONFIG_ZERO_LATENCY_LEVELS)
#define ZERO_LATENCY_LEVELS CONFIG_ZERO_LATENCY_LEVELS
#else
#define ZERO_LATENCY_LEVELS 1
#endif

#define _CHECK_PRIO(priority_p, flags_p) \
	BUILD_ASSERT(((flags_p & IRQ_ZERO_LATENCY) && \
		      ((ZERO_LATENCY_LEVELS == 1) || \
		       (priority_p < ZERO_LATENCY_LEVELS))) || \
		     (priority_p <= IRQ_PRIO_LOWEST), \
		     "Invalid interrupt priority. Values must not exceed IRQ_PRIO_LOWEST");

#endif /* CONFIG_CPU_CORTEX_M */

/*
 *
 */
#define ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p) \
{ \
	BUILD_ASSERT(!(flags_p & IRQ_ZERO_LATENCY), \
			"ZLI interrupts must be registered using IRQ_DIRECT_CONNECT()"); \
	_CHECK_PRIO(priority_p, flags_p) \
	Z_ISR_DECLARE(irq_p, 0, isr_p, isr_param_p); \
	z_arm_irq_priority_set(irq_p, priority_p, flags_p); \
}

#define ARCH_IRQ_DIRECT_CONNECT(irq_p, priority_p, isr_p, flags_p) \
{ \
	BUILD_ASSERT(IS_ENABLED(CONFIG_ZERO_LATENCY_IRQS) || !(flags_p & IRQ_ZERO_LATENCY), \
			"ZLI interrupt registered but feature is disabled"); \
	_CHECK_PRIO(priority_p, flags_p) \
	z_arm_irq_priority_set(irq_p, priority_p, flags_p); \
}

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */


#endif
