#include <drivers/uart.h>
#include <drivers/dma.h>
#include <irq.h>

#include "gd32_regs.h"
#include "gd32f50x_dma.h"

namespace hal {
namespace {

/* STAT bits */
constexpr uint32_t STAT_TBE   = (1U << 7);
constexpr uint32_t STAT_TC    = (1U << 6);
constexpr uint32_t STAT_RBNE  = (1U << 5);

/* CTL0 bits */
constexpr uint32_t CTL0_UEN    = (1U << 13);
constexpr uint32_t CTL0_TEN    = (1U << 3);
constexpr uint32_t CTL0_REN    = (1U << 2);
constexpr uint32_t CTL0_RBNEIE = (1U << 5);
constexpr uint32_t CTL0_PM_EVEN = 0x00000400U;
constexpr uint32_t CTL0_PM_ODD  = 0x00000600U;
constexpr uint32_t CTL0_PM_MASK = 0x00000600U;
constexpr uint32_t CTL0_WL_9BIT = 0x00001000U;

/* CTL1 bits */
constexpr uint32_t CTL1_STB_MASK = (3U << 12);
constexpr uint32_t CTL1_STB_2BIT = 0x00002000U;

/* CTL2 bits */
constexpr uint32_t CTL2_DENT = (1U << 7);
constexpr uint32_t CTL2_DENR = (1U << 6);

void usart_clock_enable(uintptr_t base) {
    auto &apb1 = gd32::rcu_apb1en();
    auto &apb2 = gd32::rcu_apb2en();
    switch (base) {
        case USART0_BASE: apb2 |= gd32::clk::USART0EN; break;
        case USART1_BASE: apb1 |= gd32::clk::USART1EN; break;
        case USART2_BASE: apb1 |= gd32::clk::USART2EN; break;
        case UART3_BASE:  apb1 |= gd32::clk::UART3EN;  break;
        case UART4_BASE:  apb1 |= gd32::clk::UART4EN;  break;
        default: break;
    }
}

} // anonymous namespace

Status UartBase::init(const UartConfig &config) {
    if (!config.rx_buffer || config.rx_buffer_size == 0) return Status::InvalidArgument;

    m_rx_ring = RingBuf(config.rx_buffer, config.rx_buffer_size);
    auto *regs = reinterpret_cast<gd32::UsartRegs *>(m_base);

    usart_clock_enable(m_base);

    regs->CTL0 &= ~CTL0_UEN;

    /* Baud rate */
    uint32_t pclk = (m_base == USART0_BASE) ? SystemCoreClock : (SystemCoreClock / 2);
    regs->BAUD = (pclk + config.baudrate / 2) / config.baudrate;

    /* Data bits */
    regs->CTL0 &= ~CTL0_WL_9BIT;
    if (config.data_bits == DataBits::Bits9)
        regs->CTL0 |= CTL0_WL_9BIT;

    /* Stop bits */
    regs->CTL1 &= ~CTL1_STB_MASK;
    if (config.stop_bits == StopBits::Two)
        regs->CTL1 |= CTL1_STB_2BIT;

    /* Parity */
    regs->CTL0 &= ~CTL0_PM_MASK;
    if (config.parity == Parity::Odd)       regs->CTL0 |= CTL0_PM_ODD;
    else if (config.parity == Parity::Even)  regs->CTL0 |= CTL0_PM_EVEN;

    /* Enable TX, RX, RX interrupt, DMA requests */
    regs->CTL0 |= CTL0_TEN | CTL0_REN | CTL0_RBNEIE;
    regs->CTL2 |= CTL2_DENT | CTL2_DENR;
    regs->CTL0 |= CTL0_UEN;

    hal::Irq::enable(m_irq);

    m_initialized = true;
    return Status::Ok;
}

Status UartBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<gd32::UsartRegs *>(m_base);
    regs->CTL0 &= ~CTL0_UEN;
    m_initialized = false;
    return Status::Ok;
}

Status UartBase::send(const uint8_t *data, size_t len, size_t *bytes_sent, uint32_t timeout_ms) {
    if (!m_initialized || !data || len == 0) return Status::InvalidArgument;

    uint32_t timeout_loops = timeout_ms * (SystemCoreClock / 1000U / 4U);
    if (timeout_loops == 0) timeout_loops = 1;

    DmaChannel tx_dma(DMA1_BASE, 4, 10);
    tx_dma.clear_flags();

    DmaConfig dma_cfg = {
        .request_id   = DMA_REQUEST_USART0_TX,
        .periph_addr  = m_base + 0x04U,
        .memory_addr  = reinterpret_cast<uint32_t>(data),
        .count        = static_cast<uint16_t>(len),
        .direction    = DmaDirection::MemoryToPeriph,
        .periph_width = DmaWidth::Byte,
        .memory_width = DmaWidth::Byte,
        .priority     = DmaPriority::Medium,
        .periph_inc   = false,
        .memory_inc   = true,
        .circular     = false,
    };

    tx_dma.config(dma_cfg);
    tx_dma.start();

    uint32_t remaining = timeout_loops;
    while (!tx_dma.is_transfer_complete()) {
        if (--remaining == 0) return Status::Timeout;
    }

    auto *regs = reinterpret_cast<gd32::UsartRegs *>(m_base);
    remaining = timeout_loops;
    while (!(regs->STAT & STAT_TC)) {
        if (--remaining == 0) return Status::Timeout;
    }

    tx_dma.stop();
    tx_dma.clear_flags();

    if (bytes_sent) *bytes_sent = len;
    return Status::Ok;
}

Status UartBase::recv(uint8_t *data, size_t len, size_t *bytes_read, uint32_t timeout_ms) {
    if (!m_initialized || !data || len == 0) return Status::InvalidArgument;

    size_t rd = m_rx_ring.read(data, len);
    if (rd > 0) {
        if (bytes_read) *bytes_read = rd;
        return Status::Ok;
    }

    if (m_rx_sem.take(timeout_ms) != 0) {
        if (bytes_read) *bytes_read = 0;
        return Status::Timeout;
    }

    rd = m_rx_ring.read(data, len);
    if (bytes_read) *bytes_read = rd;
    return Status::Ok;
}

size_t UartBase::rx_available() const {
    return m_rx_ring.size();
}

void UartBase::isr_handler() {
    auto *regs = reinterpret_cast<gd32::UsartRegs *>(m_base);
    if (regs->STAT & STAT_RBNE) {
        uint8_t ch = static_cast<uint8_t>(regs->DATA);
        m_rx_ring.write(&ch, 1);
        (void)m_rx_sem.release();
    }
}

} // namespace hal
