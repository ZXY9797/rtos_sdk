#include <drivers/uart.h>
#include <drivers/dma.h>
#include <irq.h>
#include <osal.h>

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
constexpr uint32_t CTL0_TCIE   = (1U << 6);
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

/* UART TX DMA 完成信号量指针（用于 DMA ISR → send 线程通知） */
static osal::Semaphore *s_uart_tx_sem = nullptr;

Status UartBase::init(const UartConfig &config) {
    if (!config.rx_buffer || config.rx_buffer_size == 0) return Status::InvalidArgument;

    if (!m_rx_stream.create(config.rx_buffer, config.rx_buffer_size, 1)) {
        return Status::NoMemory;
    }
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

    set_state(DeviceState::Initialized);
    return Status::Ok;
}

Status UartBase::deinit() {
    if (!is_initialized()) return Status::Ok;
    auto *regs = reinterpret_cast<gd32::UsartRegs *>(m_base);
    regs->CTL0 &= ~CTL0_UEN;
    m_rx_stream.destroy();
    set_state(DeviceState::Created);
    return Status::Ok;
}

Status UartBase::send(const uint8_t *data, size_t len, size_t *bytes_sent, uint32_t timeout_ms) {
    if (!is_initialized() || !data || len == 0) return Status::InvalidArgument;

    s_uart_tx_sem = &m_tx_sem;

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

    (void)tx_dma.config(dma_cfg);
    (void)tx_dma.start();

    /* 等待 DMA 完成（ISR 释放信号量，让出 CPU） */
    if (m_tx_sem.take(timeout_ms) != 0) {
        (void)tx_dma.stop();
        tx_dma.clear_flags();
        return Status::Timeout;
    }

    /* 等待 TC（最后一位从移位寄存器发出，ISR 释放信号量） */
    auto *regs = reinterpret_cast<gd32::UsartRegs *>(m_base);
    regs->CTL0 |= CTL0_TCIE;
    if (m_tx_sem.take(timeout_ms) != 0) {
        regs->CTL0 &= ~CTL0_TCIE;
        (void)tx_dma.stop();
        tx_dma.clear_flags();
        return Status::Timeout;
    }

    (void)tx_dma.stop();
    tx_dma.clear_flags();

    if (bytes_sent) *bytes_sent = len;
    m_stats.tx_bytes += len;
    return Status::Ok;
}

Status UartBase::recv(uint8_t *data, size_t len, size_t *bytes_read, uint32_t timeout_ms) {
    if (!is_initialized() || !data || len == 0) return Status::InvalidArgument;

    size_t rd = m_rx_stream.receive(data, len, timeout_ms);
    if (rd == 0) {
        if (bytes_read) *bytes_read = 0;
        return Status::Timeout;
    }
    if (bytes_read) *bytes_read = rd;
    m_stats.rx_bytes += rd;
    return Status::Ok;
}

size_t UartBase::rx_available() const {
    return m_rx_stream.bytes_available();
}

void UartBase::isr_handler() {
    auto *regs = reinterpret_cast<gd32::UsartRegs *>(m_base);
    uint32_t stat = regs->STAT;

    if (stat & STAT_RBNE) {
        uint8_t ch = static_cast<uint8_t>(regs->DATA);
        int woken = 0;
        (void)m_rx_stream.send_from_isr(&ch, 1, &woken);
        (void)woken;
    }

    /* TC 中断：传输完成，释放 send 信号量 */
    if ((stat & STAT_TC) && (regs->CTL0 & CTL0_TCIE)) {
        regs->CTL0 &= ~CTL0_TCIE;
        (void)m_tx_sem.release();
    }
}

} // namespace hal

/* DMA1 Channel 4 中断 — UART0 TX DMA 完成 */
extern "C" void IRQ60_Handler(void) {
    auto *dma = reinterpret_cast<gd32::DmaRegs *>(DMA1_BASE);
    dma->INTC = (DMA_INTC_GIFC | DMA_INTC_FTFIFC
                 | DMA_INTC_HTFIFC | DMA_INTC_ERRIFC) << (4U * 4U);
    if (hal::s_uart_tx_sem) {
        (void)hal::s_uart_tx_sem->release();
    }
}
