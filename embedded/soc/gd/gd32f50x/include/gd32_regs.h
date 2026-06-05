/**
 * @brief GD32F50x 外设寄存器结构体定义
 *
 * 集中定义所有外设寄存器布局，供驱动 .cc 使用。
 * 消除各驱动文件重复的寄存器定义和 RCU 宏。
 */
#pragma once

#include <cstdint>
#include "gd32f50x.h"

namespace gd32 {

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

/* ─── RCU 寄存器访问 ────────────────────────────────────────────── */
/* 使用 inline 函数避免宏污染全局命名空间 */

inline volatile uint32_t &rcu_apb1en() {
    return *reinterpret_cast<volatile uint32_t *>(RCU_BASE + 0x1CU);
}

inline volatile uint32_t &rcu_apb2en() {
    return *reinterpret_cast<volatile uint32_t *>(RCU_BASE + 0x18U);
}

inline volatile uint32_t &rcu_ahben() {
    return *reinterpret_cast<volatile uint32_t *>(RCU_BASE + 0x14U);
}

/* RCU 时钟使能位 */
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

    /* AHB */
    constexpr uint32_t GPIOAEN  = (1U << 0);
    constexpr uint32_t GPIOBEN  = (1U << 1);
    constexpr uint32_t GPIOCEN  = (1U << 2);
    constexpr uint32_t GPIODEN  = (1U << 3);
    constexpr uint32_t GPIOEEN  = (1U << 4);
    constexpr uint32_t GPIOFEN  = (1U << 5);
    constexpr uint32_t GPIOGEN  = (1U << 6);
    constexpr uint32_t GPIOHEN  = (1U << 7);
    constexpr uint32_t GPIOIEN  = (1U << 8);
} // namespace clk

} // namespace gd32
