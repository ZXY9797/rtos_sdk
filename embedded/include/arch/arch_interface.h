#ifndef ZEPHYR_INCLUDE_ARCH_ARCH_INTERFACE_H_
#define ZEPHYR_INCLUDE_ARCH_ARCH_INTERFACE_H_

#ifndef _ASMLANGUAGE
#include <toolchain.h>
#include <stddef.h>
#include <stdint.h>


/**
 * @addtogroup arch-irq
 * @{
 */
/**
 * Lock interrupts on the current CPU
 *
 * @see irq_lock()
 */
static inline unsigned int arch_irq_lock(void);

/**
 * Unlock interrupts on the current CPU
 *
 * @see irq_unlock()
 */
static inline void arch_irq_unlock(unsigned int key);

/**
 * Test if calling arch_irq_unlock() with this key would unlock irqs
 *
 * @param key value returned by arch_irq_lock()
 * @return true if interrupts were unlocked prior to the arch_irq_lock()
 * call that produced the key argument.
 */
static inline bool arch_irq_unlocked(unsigned int key);

/**
 * Disable the specified interrupt line
 *
 * @note: The behavior of interrupts that arrive after this call
 * returns and before the corresponding call to arch_irq_enable() is
 * undefined.  The hardware is not required to latch and deliver such
 * an interrupt, though on some architectures that may work.  Other
 * architectures will simply lose such an interrupt and never deliver
 * it.  Many drivers and subsystems are not tolerant of such dropped
 * interrupts and it is the job of the application layer to ensure
 * that behavior remains correct.
 *
 * @see irq_disable()
 */
void arch_irq_disable(unsigned int irq);

/**
 * Enable the specified interrupt line
 *
 * @see irq_enable()
 */
void arch_irq_enable(unsigned int irq);

/**
 * Test if an interrupt line is enabled
 *
 * @see irq_is_enabled()
 */
int arch_irq_is_enabled(unsigned int irq);

/**
 * Arch-specific hook to install a dynamic interrupt.
 *
 * @param irq IRQ line number
 * @param priority Interrupt priority
 * @param routine Interrupt service routine
 * @param parameter ISR parameter
 * @param flags Arch-specific IRQ configuration flag
 *
 * @return The vector assigned to this interrupt
 */
int arch_irq_connect_dynamic(unsigned int irq, unsigned int priority,
			     void (*routine)(const void *parameter),
			     const void *parameter, uint32_t flags);

/**
 * Arch-specific hook to dynamically uninstall a shared interrupt.
 * If the interrupt is not being shared, then the associated
 * _sw_isr_table entry will be replaced by (NULL, z_irq_spurious)
 * (default entry).
 *
 * @param irq IRQ line number
 * @param priority Interrupt priority
 * @param routine Interrupt service routine
 * @param parameter ISR parameter
 * @param flags Arch-specific IRQ configuration flag
 *
 * @return 0 in case of success, negative value otherwise
 */
int arch_irq_disconnect_dynamic(unsigned int irq, unsigned int priority,
				void (*routine)(const void *parameter),
				const void *parameter, uint32_t flags);

#include <arch/arch_inlines.h>

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_ARCH_INTERFACE_H_ */
