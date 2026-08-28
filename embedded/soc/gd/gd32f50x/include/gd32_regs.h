/**
 * @brief GD32F50x peripheral register layouts.
 *
 * Centralized layouts prevent divergent register definitions in drivers.
 */
#pragma once

#include <cstdint>
#include "gd32f50x.h"

namespace gd32 {

static_assert(TIMER0_BASE == 0x40012C00U);
static_assert(ADC0_BASE == 0x40012400U);
static_assert(GPIOA_BASE == 0x40010800U);
static_assert(RCU_BASE == 0x40021000U);

/* ─── USART ─────────────────────────────────────────────────────── */

struct UsartRegs {
    volatile uint32_t STAT;   // 0x00
    volatile uint32_t DATA;   // 0x04
    volatile uint32_t BAUD;   // 0x08
    volatile uint32_t CTL0;   // 0x0C
    volatile uint32_t CTL1;   // 0x10
    volatile uint32_t CTL2;   // 0x14
};

/* ─── SPI ───────────────────────────────────────────────────────── */

struct SpiRegs {
    volatile uint32_t CTL0;   // 0x00
    volatile uint32_t CTL1;   // 0x04
    volatile uint32_t STAT;   // 0x08
    volatile uint32_t DATA;   // 0x0C
};

/* ─── GPIO ──────────────────────────────────────────────────────── */

struct GpioRegs {
    volatile uint32_t CTL;      // 0x00 - mode control (2 bits/pin)
    volatile uint32_t OMODE;    // 0x04 - output type (1 bit/pin)
    volatile uint32_t OSPD;     // 0x08 - output speed (2 bits/pin)
    volatile uint32_t PUD;      // 0x0C - pull-up/down (2 bits/pin)
    volatile uint32_t ISTAT;    // 0x10 - input status (read-only)
    volatile uint32_t OCTL;     // 0x14 - output control
    volatile uint32_t BOP;      // 0x18 - bit operation (set low, clear high)
    volatile uint32_t LOCK;     // 0x1C - config lock
    volatile uint32_t AFSEL0;   // 0x20 - alt func pins 0-7 (4 bits/pin)
    volatile uint32_t AFSEL1;   // 0x24 - alt func pins 8-15 (4 bits/pin)
    volatile uint32_t BC;       // 0x28 - bit clear
};

/* ─── DMA ───────────────────────────────────────────────────────── */

struct DmaChannelRegs {
    volatile uint32_t CTL;      // +0x00
    volatile uint32_t CNT;      // +0x04
    volatile uint32_t PADDR;    // +0x08
    volatile uint32_t MADDR;    // +0x0C
    volatile uint32_t RESERVED; // +0x10 (padding to stride 0x14)
};

struct DmaRegs {
    volatile uint32_t INTF;     // 0x00 - interrupt flag
    volatile uint32_t INTC;     // 0x04 - interrupt flag clear
    DmaChannelRegs CH[7];       // 0x08 - channels 0..6
};

/* ─── TIMER ─────────────────────────────────────────────────────── */

struct TimerRegs {
    volatile uint32_t CTL0;     // 0x00
    volatile uint32_t CTL1;     // 0x04
    volatile uint32_t SMCFG;    // 0x08
    volatile uint32_t DMAINTEN; // 0x0C
    volatile uint32_t INTF;     // 0x10
    volatile uint32_t SWEVG;    // 0x14
    volatile uint32_t CHCTL0;   // 0x18
    volatile uint32_t CHCTL1;   // 0x1C
    volatile uint32_t CHCTL2;   // 0x20
    volatile uint32_t CNT;      // 0x24
    volatile uint32_t PSC;      // 0x28
    volatile uint32_t CAR;      // 0x2C
    volatile uint32_t CREP;     // 0x30
    volatile uint32_t CH0CV;    // 0x34
    volatile uint32_t CH1CV;    // 0x38
    volatile uint32_t CH2CV;    // 0x3C
    volatile uint32_t CH3CV;    // 0x40
    volatile uint32_t CCHP;     // 0x44
    volatile uint32_t DMACFG;   // 0x48
    volatile uint32_t DMATB;    // 0x4C
};

/* ─── ADC ───────────────────────────────────────────────────────── */

struct AdcRegs {
    volatile uint32_t STAT;       // 0x00
    volatile uint32_t CTL0;       // 0x04
    volatile uint32_t CTL1;       // 0x08
    volatile uint32_t SAMPT0;     // 0x0C
    volatile uint32_t SAMPT1;     // 0x10
    volatile uint32_t IOFF[4];    // 0x14-0x20
    volatile uint32_t WDHIGH;     // 0x24
    volatile uint32_t WDLOW;      // 0x28
    volatile uint32_t RSQ0;       // 0x2C
    volatile uint32_t RSQ1;       // 0x30
    volatile uint32_t RSQ2;       // 0x34
    volatile uint32_t ISQ;        // 0x38
    volatile uint32_t LDATA[4];   // 0x3C-0x48
    volatile uint32_t RDATA;      // 0x4C
    volatile uint32_t IDATA;      // 0x50
    volatile uint32_t LDCTL;      // 0x54
    volatile uint32_t RESERVED[10];
    volatile uint32_t OVSAMPCTL;  // 0x80
};

/* RCU register accessors avoid leaking address macros. */

inline volatile uint32_t &rcu_apb1en() {
    return *reinterpret_cast<volatile uint32_t *>(RCU_BASE + 0x1CU);
}

inline volatile uint32_t &rcu_apb2en() {
    return *reinterpret_cast<volatile uint32_t *>(RCU_BASE + 0x18U);
}

inline volatile uint32_t &rcu_cfg0() {
    return *reinterpret_cast<volatile uint32_t *>(RCU_BASE + 0x04U);
}

inline volatile uint32_t &rcu_cfg1() {
    return *reinterpret_cast<volatile uint32_t *>(RCU_BASE + 0x2CU);
}

inline volatile uint32_t &rcu_ahben() {
    return *reinterpret_cast<volatile uint32_t *>(RCU_BASE + 0x14U);
}

/* RCU clock enable bits. */
namespace clk {
    /* APB2 */
    constexpr uint32_t USART0EN = (1U << 4);
    constexpr uint32_t SPI0EN   = (1U << 12);

    /* APB1 */
    constexpr uint32_t USART1EN = (1U << 17);
    constexpr uint32_t USART2EN = (1U << 18);
    constexpr uint32_t UART3EN  = (1U << 19);
    constexpr uint32_t UART4EN  = (1U << 20);
    constexpr uint32_t SPI1EN   = (1U << 14);
    constexpr uint32_t SPI2EN   = (1U << 15);

    /* APB2 */
    constexpr uint32_t TIMER0EN = (1U << 11);
    constexpr uint32_t ADC0EN   = (1U << 9);

    /* APB2 */
    constexpr uint32_t GPIOAEN  = (1U << 2);
    constexpr uint32_t GPIOBEN  = (1U << 3);
    constexpr uint32_t GPIOCEN  = (1U << 4);
    constexpr uint32_t GPIODEN  = (1U << 5);
    constexpr uint32_t GPIOEEN  = (1U << 6);
} // namespace clk

} // namespace gd32
