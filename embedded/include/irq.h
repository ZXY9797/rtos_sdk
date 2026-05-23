#ifndef ZEPHYR_INCLUDE_IRQ_H_
#define ZEPHYR_INCLUDE_IRQ_H_
#include <arch/cpu.h>
#ifndef _ASMLANGUAGE
#include <toolchain.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Declare arch_irq_connect_dynamic */
extern int arch_irq_connect_dynamic(unsigned int irq, unsigned int priority,
				    void (*routine)(const void *parameter),
				    const void *parameter, uint32_t flags);

/**
 * @brief Initialize an interrupt handler.
 *
 * This routine initializes an interrupt handler for an IRQ. The IRQ must be
 * subsequently enabled before the interrupt handler begins servicing
 * interrupts.
 *
 * @warning
 * Although this routine is invoked at run-time, all of its arguments must be
 * computable by the compiler at build time.
 *
 * @param irq_p IRQ line number.
 * @param priority_p Interrupt priority.
 * @param isr_p Address of interrupt service routine.
 * @param isr_param_p Parameter passed to interrupt service routine.
 * @param flags_p Architecture-specific IRQ configuration flags..
 */
#define IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p) \
	ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p)

/**
 * Configure a dynamic interrupt.
 *
 * Use this instead of IRQ_CONNECT() if arguments cannot be known at build time.
 *
 * @param irq IRQ line number
 * @param priority Interrupt priority
 * @param routine Interrupt service routine
 * @param parameter ISR parameter
 * @param flags Arch-specific IRQ configuration flags
 *
 * @return The vector assigned to this interrupt
 */
static inline int
irq_connect_dynamic(unsigned int irq, unsigned int priority,
		    void (*routine)(const void *parameter),
		    const void *parameter, uint32_t flags)
{
	return arch_irq_connect_dynamic(irq, priority, routine, parameter,
					flags);
}

/**
 * Disconnect a dynamic interrupt.
 *
 * Use this in conjunction with shared interrupts to remove a routine/parameter
 * pair from the list of clients using the same interrupt line. If the interrupt
 * is not being shared then the associated _sw_isr_table entry will be replaced
 * by (NULL, z_irq_spurious) (default entry).
 *
 * @param irq IRQ line number
 * @param priority Interrupt priority
 * @param routine Interrupt service routine
 * @param parameter ISR parameter
 * @param flags Arch-specific IRQ configuration flags
 *
 * @return 0 in case of success, negative value otherwise
 */
static inline int
irq_disconnect_dynamic(unsigned int irq, unsigned int priority,
		       void (*routine)(const void *parameter),
		       const void *parameter, uint32_t flags)
{
	// return arch_irq_disconnect_dynamic(irq, priority, routine,
	// 				   parameter, flags);
    return 0;
}

/**
 * @brief Initialize a 'direct' interrupt handler.
 *
 * This routine initializes an interrupt handler for an IRQ. The IRQ must be
 * subsequently enabled via irq_enable() before the interrupt handler begins
 * servicing interrupts.
 *
 * These ISRs are designed for performance-critical interrupt handling and do
 * not go through common interrupt handling code. They must be implemented in
 * such a way that it is safe to put them directly in the vector table.  For
 * ISRs written in C, The ISR_DIRECT_DECLARE() macro will do this
 * automatically. For ISRs written in assembly it is entirely up to the
 * developer to ensure that the right steps are taken.
 *
 * This type of interrupt currently has a few limitations compared to normal
 * Zephyr interrupts:
 * - No parameters are passed to the ISR.
 * - No stack switch is done, the ISR will run on the interrupted context's
 *   stack, unless the architecture automatically does the stack switch in HW.
 * - Interrupt locking state is unchanged from how the HW sets it when the ISR
 *   runs. On arches that enter ISRs with interrupts locked, they will remain
 *   locked.
 * - Scheduling decisions are now optional, controlled by the return value of
 *   ISRs implemented with the ISR_DIRECT_DECLARE() macro
 * - The call into the OS to exit power management idle state is now optional.
 *   Normal interrupts always do this before the ISR is run, but when it runs
 *   is now controlled by the placement of a ISR_DIRECT_PM() macro, or omitted
 *   entirely.
 *
 * @warning
 * Although this routine is invoked at run-time, all of its arguments must be
 * computable by the compiler at build time.
 *
 * @note
 * All IRQs configured with the IRQ_ZERO_LATENCY flag must be declared as
 * direct.
 *
 * @param irq_p IRQ line number.
 * @param priority_p Interrupt priority.
 * @param isr_p Address of interrupt service routine.
 * @param flags_p Architecture-specific IRQ configuration flags.
 */
#define IRQ_DIRECT_CONNECT(irq_p, priority_p, isr_p, flags_p) \
	ARCH_IRQ_DIRECT_CONNECT(irq_p, priority_p, isr_p, flags_p)

/**
 * @brief Lock interrupts.
 * @def irq_lock()
 *
 * This routine disables all interrupts on the CPU. It returns an unsigned
 * integer "lock-out key", which is an architecture-dependent indicator of
 * whether interrupts were locked prior to the call. The lock-out key must be
 * passed to irq_unlock() to re-enable interrupts.
 *
 * @note
 * This routine must also serve as a memory barrier to ensure the uniprocessor
 * implementation of spinlocks is correct.
 *
 * This routine can be called recursively, as long as the caller keeps track
 * of each lock-out key that is generated. Interrupts are re-enabled by
 * passing each of the keys to irq_unlock() in the reverse order they were
 * acquired. (That is, each call to irq_lock() must be balanced by
 * a corresponding call to irq_unlock().)
 *
 * This routine can only be invoked from supervisor mode. Some architectures
 * (for example, ARM) will fail silently if invoked from user mode instead
 * of generating an exception.
 *
 * This routine can be called by ISRs and threads.
 *
 * @warning
 * As long as all recursive calls to irq_lock() have not been balanced with
 * corresponding irq_unlock() calls, the caller "holds the interrupt lock".
 *
 * "Holding the interrupt lock" when a context switch occurs is illegal.
 *
 * @warning
 * The lock-out key should never be used to manually re-enable interrupts
 * or to inspect or manipulate the contents of the CPU's interrupt bits.
 *
 * @return An architecture-dependent lock-out key representing the
 *         "interrupt disable state" prior to the call.
 */
#ifdef CONFIG_SMP
unsigned int z_smp_global_lock(void);
#define irq_lock() z_smp_global_lock()
#else
#define irq_lock() arch_irq_lock()
#endif

/**
 * @brief Unlock interrupts.
 * @def irq_unlock()
 *
 * This routine reverses the effect of a previous call to irq_lock() using
 * the associated lock-out key. The caller must call the routine once for
 * each time it called irq_lock(), supplying the keys in the reverse order
 * they were acquired, before interrupts are enabled.
 *
 * @note
 * This routine must also serve as a memory barrier to ensure the uniprocessor
 * implementation of spinlocks is correct.
 *
 * This routine can only be invoked from supervisor mode. Some architectures
 * (for example, ARM) will fail silently if invoked from user mode instead
 * of generating an exception.
 *
 * @note Can be called by ISRs.
 *
 * @param key Lock-out key generated by irq_lock().
 */
#ifdef CONFIG_SMP
void z_smp_global_unlock(unsigned int key);
#define irq_unlock(key) z_smp_global_unlock(key)
#else
#define irq_unlock(key) arch_irq_unlock(key)
#endif

/**
 * @brief Enable an IRQ.
 *
 * This routine enables interrupts from source @a irq.
 *
 * @param irq IRQ line.
 */
#define irq_enable(irq) arch_irq_enable(irq)

/**
 * @brief Disable an IRQ.
 *
 * This routine disables interrupts from source @a irq.
 *
 * @param irq IRQ line.
 */
#define irq_disable(irq) arch_irq_disable(irq)

/**
 * @brief Get IRQ enable state.
 *
 * This routine indicates if interrupts from source @a irq are enabled.
 *
 * @param irq IRQ line.
 *
 * @return interrupt enable state, true or false
 */
#define irq_is_enabled(irq) arch_irq_is_enabled(irq)

#ifdef __cplusplus
}
#endif
#endif /* ASMLANGUAGE */
#endif

