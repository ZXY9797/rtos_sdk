#include <init.h>
#include <soc.h>
#include <devicetree.h>
#include <devicetree/clocks.h>
#include <device.h>
#include <drivers/clock_control.h>
#include <osal.h>

#if defined(CONFIG_TIMER_READS_ITS_FREQUENCY_AT_RUNTIME)
extern unsigned int z_clock_hw_cycles_per_sec;
#define CYC_PER_TICK (z_clock_hw_cycles_per_sec/CONFIG_SYS_CLOCK_TICKS_PER_SEC)
#else
#define CYC_PER_TICK (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC/CONFIG_SYS_CLOCK_TICKS_PER_SEC)
#endif

/* add MAX_TICKS protection */
#define _MAX_TICKS (int)((k_ticks_t)(COUNTER_MAX / CYC_PER_TICK) - 1)
#define MAX_TICKS ((_MAX_TICKS > 0) ? _MAX_TICKS : 1)
#define MAX_CYCLES (MAX_TICKS * CYC_PER_TICK)

#if (COUNTER_MAX / CYC_PER_TICK) == 1
#pragma message("tickless does nothing as CONFIG_SYS_CLOCK_TICKS_PER_SEC too low")
#endif

/* Minimum cycles in the future to try to program.  Note that this is
 * NOT simply "enough cycles to get the counter read and reprogrammed
 * reliably" -- it becomes the minimum value of the LOAD register, and
 * thus reflects how much time we can reliably see expire between
 * calls to elapsed() to read the COUNTFLAG bit.  So it needs to be
 * set to be larger than the maximum time the interrupt might be
 * masked.  Choosing a fraction of a tick is probably a good enough
 * default, with an absolute minimum of 1k cyc.
 */
#define MIN_DELAY MAX(1024U, ((uint32_t)CYC_PER_TICK/16U))

static uint32_t last_load;

typedef uint64_t cycle_t;

/*
 * This local variable holds the amount of SysTick HW cycles elapsed
 * and it is updated in sys_clock_isr() and sys_clock_set_timeout().
 *
 * Note:
 *  At an arbitrary point in time the "current" value of the SysTick
 *  HW timer is calculated as:
 *
 * t = cycle_counter + elapsed();
 */
static cycle_t cycle_count;

/*
 * This local variable holds the amount of elapsed SysTick HW cycles
 * that have been announced to the kernel.
 *
 * Note:
 * Additions/subtractions/comparisons of 64-bits values on 32-bits systems
 * are very cheap. Divisions are not. Make sure the difference between
 * cycle_count and announced_cycles is stored in a 32-bit variable before
 * dividing it by CYC_PER_TICK.
 */
static cycle_t announced_cycles;

/*
 * This local variable holds the amount of elapsed HW cycles due to
 * SysTick timer wraps ('overflows') and is used in the calculation
 * in elapsed() function, as well as in the updates to cycle_count.
 *
 * Note:
 * Each time cycle_count is updated with the value from overflow_cyc,
 * the overflow_cyc must be reset to zero.
 */
static volatile uint32_t overflow_cyc;

/* This internal function calculates the amount of HW cycles that have
 * elapsed since the last time the absolute HW cycles counter has been
 * updated. 'cycle_count' may be updated either by the ISR, or when we
 * re-program the SysTick.LOAD register, in sys_clock_set_timeout().
 *
 * Additionally, the function updates the 'overflow_cyc' counter, that
 * holds the amount of elapsed HW cycles due to (possibly) multiple
 * timer wraps (overflows).
 *
 * Prerequisites:
 * - reprogramming of SysTick.LOAD must be clearing the SysTick.COUNTER
 *   register and the 'overflow_cyc' counter.
 * - ISR must be clearing the 'overflow_cyc' counter.
 * - no more than one counter-wrap has occurred between
 *     - the timer reset or the last time the function was called
 *     - and until the current call of the function is completed.
 * - the function is invoked with interrupts disabled.
 */
static uint32_t elapsed(void)
{
	uint32_t val1 = SysTick->VAL;	/* A */
	uint32_t ctrl = SysTick->CTRL;	/* B */
	uint32_t val2 = SysTick->VAL;	/* C */

	/* SysTick behavior: The counter wraps after zero automatically.
	 * The COUNTFLAG field of the CTRL register is set when it
	 * decrements from 1 to 0. Reading the control register
	 * automatically clears that field. When a timer is started,
	 * count begins at zero then wraps after the first cycle.
	 * Reference:
	 *  Armv6-m (B3.3.1) https://developer.arm.com/documentation/ddi0419
	 *  Armv7-m (B3.3.1) https://developer.arm.com/documentation/ddi0403
	 *  Armv8-m (B11.1)  https://developer.arm.com/documentation/ddi0553
	 *
	 * First, manually wrap/realign val1 and val2 from [0:last_load-1]
	 * to [1:last_load]. This allows subsequent code to assume that
	 * COUNTFLAG and wrapping occur on the same cycle.
	 *
	 * If the count wrapped...
	 * 1) Before A then COUNTFLAG will be set and val1 >= val2
	 * 2) Between A and B then COUNTFLAG will be set and val1 < val2
	 * 3) Between B and C then COUNTFLAG will be clear and val1 < val2
	 * 4) After C we'll see it next time
	 *
	 * So the count in val2 is post-wrap and last_load needs to be
	 * added if and only if COUNTFLAG is set or val1 < val2.
	 */
	if (val1 == 0) {
		val1 = last_load;
	}
	if (val2 == 0) {
		val2 = last_load;
	}

	if ((ctrl & SysTick_CTRL_COUNTFLAG_Msk)
	    || (val1 < val2)) {
		overflow_cyc += last_load;

		/* We know there was a wrap, but we might not have
		 * seen it in CTRL, so clear it. */
		(void)SysTick->CTRL;
	}

	return (last_load - val2) + overflow_cyc;
}

/* sys_clock_isr is calling directly from the platform's vectors table.
 * However using ISR_DIRECT_DECLARE() is not so suitable due to possible
 * tracing overflow, so here is a stripped down version of it.
 */
void sys_clock_isr(void)
{
#ifdef CONFIG_TRACING_ISR
	sys_trace_isr_enter();
#endif /* CONFIG_TRACING_ISR */

	uint32_t dcycles;
	uint32_t dticks;

	/* Update overflow_cyc and clear COUNTFLAG by invoking elapsed() */
	elapsed();

	/* Increment the amount of HW cycles elapsed (complete counter
	 * cycles) and announce the progress to the kernel.
	 */
	cycle_count += overflow_cyc;
	overflow_cyc = 0;

#if defined(CONFIG_CORTEX_M_SYSTICK_LPM_TIMER_COUNTER)
	/* Rare case, when the interrupt was triggered, with previously programmed
	 * LOAD value, just before entering the idle mode (SysTick is clocked) or right
	 * after exiting the idle mode, before executing the procedure in the
	 * sys_clock_idle_exit function.
	 */
	if (timeout_idle) {
		ISR_DIRECT_PM();
		z_arm_int_exit();

		return;
	}
#endif /* CONFIG_CORTEX_M_SYSTICK_LPM_TIMER_COUNTER */

	if (IS_ENABLED(CONFIG_TICKLESS_KERNEL)) {
		/* In TICKLESS mode, the SysTick.LOAD is re-programmed
		 * in sys_clock_set_timeout(), followed by resetting of
		 * the counter (VAL = 0).
		 *
		 * If a timer wrap occurs right when we re-program LOAD,
		 * the ISR is triggered immediately after sys_clock_set_timeout()
		 * returns; in that case we shall not increment the cycle_count
		 * because the value has been updated before LOAD re-program.
		 *
		 * We can assess if this is the case by inspecting COUNTFLAG.
		 */

		dcycles = cycle_count - announced_cycles;
		dticks = dcycles / CYC_PER_TICK;
		announced_cycles += dticks * CYC_PER_TICK;
		sys_clock_announce(dticks);
	} else {
		sys_clock_announce(1);
	}

	// ISR_DIRECT_PM();

#ifdef CONFIG_TRACING_ISR
	sys_trace_isr_exit();
#endif /* CONFIG_TRACING_ISR */

	// z_arm_int_exit();
}

void sys_clock_disable(void)
{
	SysTick->CTRL &= ~SysTick_CTRL_ENABLE_Msk;
}

void SysTick_Handler(void)
{
	osal_interrupt_enter();
	sys_clock_isr();
	osal_interrupt_leave();
}

static int sys_clock_driver_init(void)
{
	NVIC_SetPriority(SysTick_IRQn, 1);
	last_load = CYC_PER_TICK;
	overflow_cyc = 0U;
	SysTick->LOAD = last_load - 1;
	SysTick->VAL = 0; /* resets timer to last_load */
	SysTick->CTRL |= (SysTick_CTRL_ENABLE_Msk |
			  SysTick_CTRL_TICKINT_Msk |
			  SysTick_CTRL_CLKSOURCE_Msk);
	return 0;
}

SYS_INIT(sys_clock_driver_init, PRE_KERNEL_2,
	 CONFIG_SYSTEM_CLOCK_INIT_PRIORITY);
