/**************************************************************************//**
 * @file     system_gr5525x.c
 * @brief    CMSIS Device System Source File for GR5525x
 *
 * @details  This file provides:
 *           - SystemInit() called from startup before main()
 *           - SystemCoreClock variable (updated by SDK platform code)
 *           - VTOR relocation to RAM vector table
 *
 * @note     GR5525 uses Cortex-M4F @ 96MHz (CPLL), flash at 0x00200000.
 *           Detailed clock/PMU calibration is handled by the SDK's
 *           platform_init() -> second_class_task() after SystemInit().
 ******************************************************************************/

#include <stdint.h>
#include "gr55xx.h"

/*
 * Default system clock: 64 MHz (IRC/HF_OSC before PLL is configured).
 * The SDK's platform code will switch to 96 MHz (CPLL_S96M_CLK) later.
 */
uint32_t SystemCoreClock = CLK_64M;

/*
 * Slow clock for RTC / sleep timer / AON WDT (32.768 kHz crystal default).
 */
uint32_t SystemSlowClock = 32768UL;

/*
 * RAM-based vector table used by the SDK for dynamic ISR registration.
 * Placed in a dedicated .bss section, 256-byte aligned (VTOR requirement).
 * MAX_NUMS_IRQn = 66, plus 16 Cortex-M system exceptions = 82 entries.
 */
#define VECTOR_TABLE_SIZE  (MAX_NUMS_IRQn + 16)
__ALIGNED(0x100) uint32_t ram_vector_table[VECTOR_TABLE_SIZE];

/*----------------------------------------------------------------------------
  SystemInit
 *----------------------------------------------------------------------------*/
void SystemInit(void)
{
    /*
     * 1. Enable FPU (CP10 / CP11 full access).
     *    GR5525 has __FPU_PRESENT = 1; set CPACR before any FPU instruction.
     */
#if (__FPU_PRESENT == 1U)
    SCB->CPACR |= ((3UL << (10U * 2U)) |   /* CP10 full access */
                    (3UL << (11U * 2U)));    /* CP11 full access */
#endif

    /*
     * 2. Copy the flash vector table into RAM and relocate VTOR.
     *    This allows the SDK (and application) to dynamically register
     *    interrupt handlers via soc_register_nvic() at runtime.
     */
    {
        uint32_t flash_vtor = SCB->VTOR;
        uint32_t i;

        for (i = 0; i < VECTOR_TABLE_SIZE; i++)
        {
            ram_vector_table[i] = *(volatile uint32_t *)(flash_vtor + (i * 4U));
        }

        __DSB();
        SCB->VTOR = (uint32_t)ram_vector_table;
        __DSB();
        __ISB();
    }

    /*
     * 3. Flash wait states for 96 MHz operation.
     *    GR5525 external flash is accessed via XQSPI; the actual wait state
     *    configuration is done by hal_flash_init() in the SDK's
     *    first_class_task().  No action needed here.
     */

    /*
     * 4. Clock initialization.
     *    After reset the system runs from the internal HF oscillator (64 MHz).
     *    Full PLL configuration (HXTAL -> CPLL -> 96 MHz) is performed by
     *    platform_clock_init() / SystemCoreSetClock() inside the SDK's
     *    second_class_task().  We only set the default value here.
     */
}
