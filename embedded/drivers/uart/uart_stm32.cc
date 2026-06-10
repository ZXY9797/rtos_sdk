#include <drivers/uart.h>
#include <drivers_generated.h>
#include <irq.h>

namespace hal {

// USART 寄存器结构体（映射 STM32H7 USART）
struct UartRegs {
    volatile uint32_t CR1;    // 0x00
    volatile uint32_t CR2;    // 0x04
    volatile uint32_t CR3;    // 0x08
    volatile uint32_t BRR;    // 0x0C
    volatile uint32_t GTPR;   // 0x10
    volatile uint32_t RTOR;   // 0x14
    volatile uint32_t RQR;    // 0x18
    volatile uint32_t ISR;    // 0x1C
    volatile uint32_t ICR;    // 0x20
    volatile uint32_t RDR;    // 0x24
    volatile uint32_t TDR;    // 0x28
    volatile uint32_t PRESC;  // 0x2C
};

// CR1
constexpr uint32_t CR1_UE     = (1U << 0);
constexpr uint32_t CR1_TE     = (1U << 3);
constexpr uint32_t CR1_RE     = (1U << 2);
constexpr uint32_t CR1_RXNEIE = (1U << 5);
constexpr uint32_t CR1_TCIE   = (1U << 6);
constexpr uint32_t CR1_M0     = (1U << 12);
constexpr uint32_t CR1_M1     = (1U << 28);
constexpr uint32_t CR1_PCE    = (1U << 10);
constexpr uint32_t CR1_PS     = (1U << 9);
// CR2
constexpr uint32_t CR2_STOP_2 = (2U << 12);
// ISR
constexpr uint32_t ISR_TXE    = (1U << 7);
constexpr uint32_t ISR_TC     = (1U << 6);
constexpr uint32_t ISR_RXNE   = (1U << 5);
constexpr uint32_t ISR_ORE    = (1U << 3);
constexpr uint32_t ISR_FE     = (1U << 1);
constexpr uint32_t ISR_PE     = (1U << 0);
constexpr uint32_t ISR_IDLE   = (1U << 4);
constexpr uint32_t ISR_TEACK  = (1U << 21);
constexpr uint32_t ISR_REACK  = (1U << 22);
// ICR
constexpr uint32_t ICR_ORECF  = (1U << 3);
constexpr uint32_t ICR_IDLECF = (1U << 4);
constexpr uint32_t ICR_TCCF   = (1U << 6);
constexpr uint32_t ICR_PECF   = (1U << 0);
constexpr uint32_t ICR_FECF   = (1U << 1);
constexpr uint32_t ICR_NCF    = (1U << 2);

Status UartBase::init(const UartConfig &config) {
    if (!config.rx_buffer || config.rx_buffer_size == 0) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<UartRegs *>(m_base);

    if (!m_rx_stream.create(config.rx_buffer, config.rx_buffer_size, 1)) {
        return Status::NoMemory;
    }

    regs->CR1 = 0;
    while (regs->ISR & ISR_TEACK) {}
    while (regs->ISR & ISR_REACK) {}

    uint32_t cr1 = 0;
    if (config.data_bits == DataBits::Bits9) cr1 |= CR1_M0;
    else if (config.data_bits == DataBits::Bits7) cr1 |= CR1_M1;
    if (config.parity != Parity::None) {
        cr1 |= CR1_PCE;
        if (config.parity == Parity::Odd) cr1 |= CR1_PS;
    }
    regs->CR1 = cr1;
    regs->CR2 = (config.stop_bits == StopBits::Two) ? CR2_STOP_2 : 0;
    regs->BRR = SystemCoreClock / config.baudrate;
    regs->ICR = ICR_PECF | ICR_FECF | ICR_NCF | ICR_ORECF | ICR_IDLECF | ICR_TCCF;
    regs->CR1 |= CR1_RXNEIE | CR1_TE | CR1_RE | CR1_UE;

    hal::Irq::enable(m_irq);

    set_state(DeviceState::Initialized);
    return Status::Ok;
}

Status UartBase::deinit() {
    if (!is_initialized()) return Status::Ok;
    auto *regs = reinterpret_cast<UartRegs *>(m_base);
    regs->CR1 = 0;
    m_rx_stream.destroy();
    set_state(DeviceState::Created);
    return Status::Ok;
}

Status UartBase::send(const uint8_t *data, size_t len, size_t *bytes_sent, uint32_t timeout_ms) {
    if (!is_initialized() || !data || len == 0) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<UartRegs *>(m_base);

    for (size_t i = 0; i < len; i++) {
        while (!(regs->ISR & ISR_TXE)) {}
        regs->TDR = data[i];
    }

    /* 使能 TC 中断，等待最后一位发送完成（ISR 释放信号量） */
    regs->CR1 |= CR1_TCIE;
    if (m_tx_sem.take(timeout_ms) != 0) {
        regs->CR1 &= ~CR1_TCIE;
        return Status::Timeout;
    }

    m_stats.tx_bytes += len;
    if (bytes_sent) *bytes_sent = len;
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
    return Status::Ok;
}

size_t UartBase::rx_available() const {
    return m_rx_stream.bytes_available();
}

void UartBase::isr_handler() {
    auto *regs = reinterpret_cast<UartRegs *>(m_base);
    uint32_t isr = regs->ISR;
    if (isr & ISR_RXNE) {
        uint8_t ch = static_cast<uint8_t>(regs->RDR);
        int woken = 0;
        size_t written = m_rx_stream.send_from_isr(&ch, 1, &woken);
        if (written > 0) {
            m_stats.rx_bytes++;
        }
        (void)woken;
    }
    if ((isr & ISR_TC) && (regs->CR1 & CR1_TCIE)) {
        regs->ICR = ICR_TCCF;
        regs->CR1 &= ~CR1_TCIE;
        (void)m_tx_sem.release();
    }
    if (isr & ISR_ORE) {
        m_stats.overrun_count++;
        regs->ICR = ICR_ORECF;
    }
    if (isr & ISR_FE) {
        m_stats.framing_errors++;
    }
    if (isr & ISR_PE) {
        m_stats.parity_errors++;
    }
}

} // namespace hal
