#ifndef ZEPHYR_INCLUDE_ARCH_ARM_ASM_INLINE_GCC_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_ASM_INLINE_GCC_H_

#ifndef _ASMLANGUAGE

#include <cmsis_core.h>
#include <toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif
static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	unsigned int key;

#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
#if CONFIG_MP_MAX_NUM_CPUS == 1 || defined(CONFIG_ARMV8_M_BASELINE)
key = __get_PRIMASK();
__disable_irq();
#else
#error "Cortex-M0 and Cortex-M0+ require SoC specific support for cross core synchronisation."
#endif
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
	key = __get_BASEPRI();
	__set_BASEPRI_MAX(16);
	__ISB();
#elif defined(CONFIG_ARMV7_R) || defined(CONFIG_AARCH32_ARMV8_R)
		|| defined(CONFIG_ARMV7_A)
		__asm__ volatile(
			"mrs %0, cpsr;"
			"and %0, #" STRINGIFY(I_BIT) ";"
			"cpsid i;"
			: "=r" (key)
			:
			: "memory", "cc");
#else
	#error Unknown ARM architecture
#endif /* CONFIG_ARMV6_M_ARMV8_M_BASELINE */

	return key;
}

/* On Cortex-M0/M0+, this enables all interrupts if they were not
 * previously disabled.
 */

static ALWAYS_INLINE void arch_irq_unlock(unsigned int key)
{
#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
	if (key != 0U) {
		return;
	}
	__enable_irq();
	__ISB();
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
	__set_BASEPRI(key);
	__ISB();
#elif defined(CONFIG_ARMV7_R) || defined(CONFIG_AARCH32_ARMV8_R) \
	|| defined(CONFIG_ARMV7_A)
	if (key != 0U) {
		return;
	}
	__enable_irq();
#else
#error Unknown ARM architecture
#endif /* CONFIG_ARMV6_M_ARMV8_M_BASELINE */
}

static ALWAYS_INLINE bool arch_irq_unlocked(unsigned int key)
{
	/* This convention works for both PRIMASK and BASEPRI */
	return key == 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_ASM_INLINE_GCC_H_ */
