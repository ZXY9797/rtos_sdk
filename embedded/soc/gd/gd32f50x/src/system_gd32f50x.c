/*!
    \file    system_gd32f50x.c
    \brief   CMSIS Cortex-M4 Device Peripheral Access Layer Source File for GD32F50x
*/

#include "gd32f50x.h"

/* Default oscillator values */
#ifndef HXTAL_VALUE
#define HXTAL_VALUE    ((uint32_t)8000000U)
#endif

#ifndef IRC8M_VALUE
#define IRC8M_VALUE    ((uint32_t)4000000U)
#endif

/* System clock frequency (core clock) */
uint32_t SystemCoreClock = IRC8M_VALUE;

/* RCU Register Definitions */
#define RCU_CTL         REG32(RCU_BASE + 0x00U)    /* Control register */
#define RCU_CFG0        REG32(RCU_BASE + 0x04U)    /* Configuration register 0 */
#define RCU_INT         REG32(RCU_BASE + 0x0CU)    /* Interrupt register */
#define RCU_AHBEN       REG32(RCU_BASE + 0x14U)    /* AHB enable register */
#define RCU_APB1EN      REG32(RCU_BASE + 0x1CU)    /* APB1 enable register */
#define RCU_APB2EN      REG32(RCU_BASE + 0x18U)    /* APB2 enable register */

/* RCU_CTL bits */
#define RCU_CTL_IRC8MEN         BIT(0)              /* IRC8M enable */
#define RCU_CTL_IRC8MSTB        BIT(1)              /* IRC8M stable flag */
#define RCU_CTL_HXTALEN         BIT(16)             /* HXTAL enable */
#define RCU_CTL_HXTALSTB        BIT(17)             /* HXTAL stable flag */
#define RCU_CTL_PLL0EN          BIT(24)             /* PLL0 enable */
#define RCU_CTL_PLL0STB         BIT(25)             /* PLL0 stable flag */

/* RCU_CFG0 bits */
#define RCU_CFG0_SCS_MASK       BITS(1,0)           /* System clock switch mask */
#define RCU_CFG0_SCS_IRC8M      0x00000000U         /* IRC8M as system clock */
#define RCU_CFG0_SCS_HXTAL      0x00000001U         /* HXTAL as system clock */
#define RCU_CFG0_SCS_PLL0P      0x00000002U         /* PLL0P as system clock */

#define RCU_CFG0_SCSS_MASK      BITS(3,2)           /* System clock switch status mask */
#define RCU_CFG0_SCSS_IRC8M     0x00000000U         /* IRC8M used as system clock */
#define RCU_CFG0_SCSS_HXTAL     0x00000004U         /* HXTAL used as system clock */
#define RCU_CFG0_SCSS_PLL0P     0x00000008U         /* PLL0P used as system clock */

#define RCU_CFG0_AHBPSC_MASK    BITS(7,4)           /* AHB prescaler mask */
#define RCU_CFG0_AHBPSC_DIV1    0x00000000U         /* AHB = CK_SYS */

#define RCU_CFG0_APB1PSC_MASK   BITS(10,8)          /* APB1 prescaler mask */
#define RCU_CFG0_APB1PSC_DIV2   0x00000400U         /* APB1 = AHB/2 */

#define RCU_CFG0_APB2PSC_MASK   BITS(13,11)         /* APB2 prescaler mask */
#define RCU_CFG0_APB2PSC_DIV1   0x00000000U         /* APB2 = AHB */

#define RCU_CFG0_PLL0SRC_MASK   BIT(16)             /* PLL0 source mask */
#define RCU_CFG0_PLL0SRC_HXTAL  0x00000000U         /* HXTAL as PLL0 source */
#define RCU_CFG0_PLL0SRC_IRC48M BIT(16)             /* IRC48M as PLL0 source */

#define RCU_CFG0_PLL0MF_MASK    BITS(25,18)         /* PLL0 multiply factor mask */
#define RCU_CFG0_PLL0MF_MUL10   (0x08U << 18U)      /* PLL0 multiply by 10 */

/* USART Register Definitions */
#define USART_STAT(usartx)      REG32((usartx) + 0x00U)  /* Status register */
#define USART_DATA(usartx)      REG32((usartx) + 0x04U)  /* Data register */
#define USART_BAUD(usartx)      REG32((usartx) + 0x08U)  /* Baud rate register */
#define USART_CTL0(usartx)      REG32((usartx) + 0x0CU)  /* Control register 0 */
#define USART_CTL1(usartx)      REG32((usartx) + 0x10U)  /* Control register 1 */
#define USART_CTL2(usartx)      REG32((usartx) + 0x14U)  /* Control register 2 */

/* USART_STAT bits */
#define USART_STAT_TBE          BIT(7)              /* Transmit buffer empty */
#define USART_STAT_TC           BIT(6)              /* Transmission complete */
#define USART_STAT_RBNE         BIT(5)              /* Read buffer not empty */

/* USART_CTL0 bits */
#define USART_CTL0_UEN          BIT(13)             /* USART enable */
#define USART_CTL0_TEN          BIT(3)              /* Transmitter enable */
#define USART_CTL0_REN          BIT(2)              /* Receiver enable */
#define USART_CTL0_RBNEIE       BIT(5)              /* RBNE interrupt enable */

/* GPIO Register Definitions */
#define GPIO_CTL0(gpiox)        REG32((gpiox) + 0x00U)  /* Port control register 0 */
#define GPIO_CTL1(gpiox)        REG32((gpiox) + 0x04U)  /* Port control register 1 */
#define GPIO_OCTL(gpiox)        REG32((gpiox) + 0x0CU)  /* Port output control register */
#define GPIO_ISTAT(gpiox)       REG32((gpiox) + 0x08U)  /* Port input status register */
#define GPIO_BOP(gpiox)         REG32((gpiox) + 0x10U)  /* Port bit operation register */
#define GPIO_BC(gpiox)          REG32((gpiox) + 0x14U)  /* Bit clear register */

/* System initialization function */
void SystemInit(void) {
    /* Enable FPU */
#if (__FPU_PRESENT == 1) && (__FPU_USED == 1)
    SCB->CPACR |= ((3UL << 10 * 2) | (3UL << 11 * 2));
#endif

    /* Enable IRC8M */
    RCU_CTL |= RCU_CTL_IRC8MEN;
    while (!(RCU_CTL & RCU_CTL_IRC8MSTB)) {}

    /* Reset clock configuration */
    RCU_CFG0 = 0x00000000U;

    /* Disable PLL */
    RCU_CTL &= ~RCU_CTL_PLL0EN;

    /* Disable all interrupts */
    RCU_INT = 0x00000000U;

    /* Enable HXTAL */
    RCU_CTL |= RCU_CTL_HXTALEN;
    while (!(RCU_CTL & RCU_CTL_HXTALSTB)) {}

    /* Set AHB prescaler to 1 */
    RCU_CFG0 &= ~RCU_CFG0_AHBPSC_MASK;
    RCU_CFG0 |= RCU_CFG0_AHBPSC_DIV1;

    /* Set APB1 prescaler to 2 */
    RCU_CFG0 &= ~RCU_CFG0_APB1PSC_MASK;
    RCU_CFG0 |= RCU_CFG0_APB1PSC_DIV2;

    /* Set APB2 prescaler to 1 */
    RCU_CFG0 &= ~RCU_CFG0_APB2PSC_MASK;
    RCU_CFG0 |= RCU_CFG0_APB2PSC_DIV1;

    /* Configure PLL: HXTAL as source, multiply by 10 */
    RCU_CFG0 &= ~(RCU_CFG0_PLL0SRC_MASK | RCU_CFG0_PLL0MF_MASK);
    RCU_CFG0 |= RCU_CFG0_PLL0SRC_HXTAL | RCU_CFG0_PLL0MF_MUL10;

    /* Enable PLL */
    RCU_CTL |= RCU_CTL_PLL0EN;
    while (!(RCU_CTL & RCU_CTL_PLL0STB)) {}

    /* Select PLL as system clock source */
    RCU_CFG0 &= ~RCU_CFG0_SCS_MASK;
    RCU_CFG0 |= RCU_CFG0_SCS_PLL0P;

    /* Wait till PLL is used as system clock source */
    while ((RCU_CFG0 & RCU_CFG0_SCSS_MASK) != RCU_CFG0_SCSS_PLL0P) {}

    /* Update SystemCoreClock */
    SystemCoreClock = HXTAL_VALUE * 10;
}
