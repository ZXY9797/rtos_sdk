#include <drivers/uart.h>
#include <drivers_generated.h>
#include <arch/arch_interface.h>
#include <irq.h>

namespace hal {

static void uart_isr_dispatch(const void *arg) {
    static_cast<UartBase *>(const_cast<void *>(arg))->isr_handler();
}

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

    m_rx_ring = RingBuf(config.rx_buffer, config.rx_buffer_size);

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
    regs->BRR = 120000000U / config.baudrate;
    regs->ICR = ICR_PECF | ICR_FECF | ICR_NCF | ICR_ORECF | ICR_IDLECF | ICR_TCCF;
    regs->CR1 |= CR1_RXNEIE | CR1_TE | CR1_RE | CR1_UE;

    arch_irq_connect_dynamic(m_irq, 0, uart_isr_dispatch, this, 0);
    arch_irq_enable(m_irq);

    m_initialized = true;
    return Status::Ok;
}

Status UartBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<UartRegs *>(m_base);
    regs->CR1 = 0;
    m_initialized = false;
    return Status::Ok;
}

Status UartBase::send(const uint8_t *data, size_t len, size_t *bytes_sent, uint32_t timeout_ms) {
    if (!m_initialized || !data || len == 0) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<UartRegs *>(m_base);
    (void)timeout_ms;

    for (size_t i = 0; i < len; i++) {
        while (!(regs->ISR & ISR_TXE)) {}
        regs->TDR = data[i];
    }
    while (!(regs->ISR & ISR_TC)) {}
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
    auto *regs = reinterpret_cast<UartRegs *>(m_base);
    uint32_t isr = regs->ISR;
    if (isr & ISR_RXNE) {
        uint8_t ch = static_cast<uint8_t>(regs->RDR);
        m_rx_ring.write(&ch, 1);
        (void)m_rx_sem.release();
    }
    if (isr & ISR_ORE) {
        regs->ICR = ICR_ORECF;
    }
}

} // namespace hal
