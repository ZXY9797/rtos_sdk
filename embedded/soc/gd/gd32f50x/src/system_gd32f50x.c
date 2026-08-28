/*
 * GD32F50x system clock initialization.
 *
 * The 200 MHz sequence follows the vendor firmware library: HXTAL / 2 * 50,
 * with staged AHB prescaler changes to limit Vcore transients.
 */

#include "gd32f50x.h"
#include "gd32f50x_fmc.h"
#include "gd32f50x_pmu.h"
#include "gd32f50x_rcu.h"

#define SYSTEM_CLOCK_HZ       ((uint32_t)200000000U)
#define CLOCK_SWITCH_DELAY    ((uint32_t)0x50U)

uint32_t SystemCoreClock = SYSTEM_CLOCK_HZ;

static void clock_fail_stop(void)
{
    __disable_irq();
    for (;;) {
        __WFI();
    }
}

static void clock_delay(uint32_t delay)
{
    volatile uint32_t index;
    for (index = 0U; index < delay * 10U; ++index) {
        __NOP();
    }
}

static int wait_for_mask(volatile uint32_t *reg, uint32_t mask,
                         uint32_t expected, uint32_t timeout)
{
    while (((*reg) & mask) != expected && timeout > 0U) {
        --timeout;
    }
    return ((*reg) & mask) == expected;
}

static void set_ahb_prescaler(uint32_t prescaler)
{
    uint32_t value = RCU_CFG0;
    value &= ~RCU_CFG0_AHBPSC;
    value |= prescaler;
    RCU_CFG0 = value;
    clock_delay(CLOCK_SWITCH_DELAY);
}

static void enable_fmc_no_wait_load(void)
{
    if ((FMC_CTL0 & FMC_CTL0_LK) != 0U) {
        FMC_KEY0 = UNLOCK_KEY0;
        FMC_KEY0 = UNLOCK_KEY1;
    }
    FMC_CTL0 |= FMC_CTL0_NWLDE;
    FMC_CTL0 |= FMC_CTL0_LK;
}

static void return_to_irc8m(void)
{
    RCU_CTL |= RCU_CTL_IRC8MEN;
    if (!wait_for_mask(&RCU_CTL, RCU_CTL_IRC8MSTB,
                       RCU_CTL_IRC8MSTB, IRC8M_STARTUP_TIMEOUT)) {
        clock_fail_stop();
    }

    if ((RCU_CFG0 & RCU_CFG0_SCSS) == RCU_SCSS_PLL0P) {
        set_ahb_prescaler(RCU_AHB_CKSYS_DIV2);
        set_ahb_prescaler(RCU_AHB_CKSYS_DIV4);
    }
    RCU_CFG0 = (RCU_CFG0 & ~RCU_CFG0_SCS) | RCU_CKSYSSRC_IRC8M;
    if (!wait_for_mask(&RCU_CFG0, RCU_CFG0_SCSS,
                       RCU_SCSS_IRC8M, IRC8M_STARTUP_TIMEOUT)) {
        clock_fail_stop();
    }
}

static void reset_clock_tree(void)
{
    RCU_CTL &= ~(RCU_CTL_HXTALEN | RCU_CTL_HCKMEN
                 | RCU_CTL_PLL0EN | RCU_CTL_PLL1EN);
    RCU_CTL &= ~RCU_CTL_HXTALBPS;
    RCU_CFG0 &= ~(RCU_CFG0_AHBPSC | RCU_CFG0_APB1PSC
                  | RCU_CFG0_APB2PSC | RCU_CFG0_ADCPSC
                  | RCU_CFG0_PLL0MF_0_3 | RCU_CFG0_PLL0MF_4_5);
    RCU_CFG1 &= ~(RCU_CFG1_PREDIV0 | RCU_CFG1_PREDIV1
                  | RCU_CFG1_PLL0SEL | RCU_CFG1_PLL1SEL);
    RCU_ADDCTL &= ~(RCU_ADDCTL_FMCSEL | RCU_ADDCTL_FMCDIV
                    | RCU_ADDCTL_PLL0DIV);
    RCU_INT = 0x01BF0000U;
    RCU_ADDINT = 0x00400000U;
}

static void configure_200mhz_hxtal(void)
{
    RCU_CTL |= RCU_CTL_HXTALEN;
    if (!wait_for_mask(&RCU_CTL, RCU_CTL_HXTALSTB,
                       RCU_CTL_HXTALSTB, HXTAL_STARTUP_TIMEOUT)) {
        clock_fail_stop();
    }

    RCU_APB1EN |= RCU_APB1EN_PMUEN;
    PMU_CTL0 |= PMU_CTL0_LDOVS;
    set_ahb_prescaler(RCU_AHB_CKSYS_DIV4);
    RCU_CFG0 = (RCU_CFG0 & ~(RCU_CFG0_APB1PSC | RCU_CFG0_APB2PSC))
        | RCU_APB1_CKAHB_DIV2 | RCU_APB2_CKAHB_DIV1;

    RCU_CFG1 = (RCU_CFG1 & ~(RCU_CFG1_PLL0SEL | RCU_CFG1_PREDIV0))
        | RCU_PLL0SRC_HXTAL | RCU_PREDIV0_DIV2;
    RCU_CFG0 = (RCU_CFG0
                & ~(RCU_CFG0_PLL0MF_0_3 | RCU_CFG0_PLL0MF_4_5))
        | RCU_PLL0_MUL50;
    RCU_CTL |= RCU_CTL_PLL0EN;
    if (!wait_for_mask(&RCU_CTL, RCU_CTL_PLL0STB,
                       RCU_CTL_PLL0STB, HXTAL_STARTUP_TIMEOUT)) {
        clock_fail_stop();
    }

    RCU_ADDCTL = (RCU_ADDCTL
                  & ~(RCU_ADDCTL_FMCSEL | RCU_ADDCTL_FMCDIV))
        | RCU_FMC_CK_AHB | RCU_FMC_DIV1;
    RCU_CFG0 = (RCU_CFG0 & ~RCU_CFG0_SCS) | RCU_CKSYSSRC_PLL0P;
    if (!wait_for_mask(&RCU_CFG0, RCU_CFG0_SCSS,
                       RCU_SCSS_PLL0P, HXTAL_STARTUP_TIMEOUT)) {
        clock_fail_stop();
    }

    set_ahb_prescaler(RCU_AHB_CKSYS_DIV2);
    set_ahb_prescaler(RCU_AHB_CKSYS_DIV1);
    SystemCoreClock = SYSTEM_CLOCK_HZ;
}

void SystemInit(void)
{
#if (__FPU_PRESENT == 1U) && (__FPU_USED == 1U)
    SCB->CPACR |= ((3UL << (10U * 2U)) | (3UL << (11U * 2U)));
#endif
    enable_fmc_no_wait_load();
    return_to_irc8m();
    reset_clock_tree();
    configure_200mhz_hxtal();
}
