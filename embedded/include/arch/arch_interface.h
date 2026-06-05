#ifndef ZEPHYR_INCLUDE_ARCH_ARCH_INTERFACE_H_
#define ZEPHYR_INCLUDE_ARCH_ARCH_INTERFACE_H_

#ifndef _ASMLANGUAGE
#include <toolchain.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup arch-irq
 * @{
 */

/**
 * Disable the specified interrupt line
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

#ifdef __cplusplus
}
#endif

#include <arch/arch_inlines.h>

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_ARCH_INTERFACE_H_ */
