#include <drivers/uart.h>
#include <irq.h>

namespace hal {
namespace {

/*
 * GR5525 UART — DesignWare 16550-compatible
 * Register layout matches uart_regs_t in gr5525_a0.h
 */
struct Gr5525UartRegs {
    union { volatile uint32_t RBR; volatile uint32_t DLL; volatile uint32_t THR; };
    union { volatile uint32_t DLH; volatile uint32_t IER; };
    union { volatile uint32_t FCR; volatile uint32_t IIR; };
    volatile uint32_t LCR;
    volatile uint32_t MCR;
    volatile uint32_t LSR;
    volatile uint32_t MSR;
    volatile uint32_t SCRATCHPAD;
};

/* LCR bits */
constexpr uint32_t LCR_DLAB    = (1U << 7);
constexpr uint32_t LCR_DLS_8   = (3U << 0);   /* 8 data bits */
constexpr uint32_t LCR_STOP_2  = (1U << 2);   /* 2 stop bits */
constexpr uint32_t LCR_PARITY_EN   = (1U << 3);
constexpr uint32_t LCR_PARITY_EVEN = (1U << 4);
constexpr uint32_t LCR_PARITY_ODD  = (0U << 4);

/* FCR bits */
constexpr uint32_t FCR_FIFOE   = (1U << 0);   /* FIFO enable */
constexpr uint32_t FCR_RFIFOR  = (1U << 1);   /* Receiver FIFO reset */
constexpr uint32_t FCR_XFIFOR  = (1U << 2);   /* Transmitter FIFO reset */

/* IER bits */
constexpr uint32_t IER_ERBFI   = (1U << 0);   /* Enable Received Data Available Interrupt */

/* LSR bits */
constexpr uint32_t LSR_DR      = (1U << 0);   /* Data Ready */
constexpr uint32_t LSR_THRE    = (1U << 5);   /* Transmit Holding Register Empty */
constexpr uint32_t LSR_TEMT    = (1U << 6);   /* Transmitter Empty */

} // anonymous namespace

Status UartBase::init(const UartConfig &config) {
    if (!config.rx_buffer || config.rx_buffer_size == 0) return Status::InvalidArgument;

    m_rx_ring = RingBuf(config.rx_buffer, config.rx_buffer_size);
    auto *regs = reinterpret_cast<Gr5525UartRegs *>(m_base);

    /* Enable FIFOs and reset both FIFOs */
    regs->FCR = FCR_FIFOE | FCR_RFIFOR | FCR_XFIFOR;

    /* Disable interrupts during configuration */
    regs->IER = 0;

    /* Set baud rate: divisor = UART_CLK / (16 * baudrate)
     * GR5525 UART clock is typically SystemCoreClock (or a divided version).
     * Access DLL/DLH by setting DLAB bit in LCR. */
    uint32_t divisor = (SystemCoreClock + config.baudrate * 8) / (config.baudrate * 16);
    if (divisor == 0) divisor = 1;

    regs->LCR |= LCR_DLAB;
    regs->DLL = divisor & 0xFF;
    regs->DLH = (divisor >> 8) & 0xFF;
    regs->LCR &= ~LCR_DLAB;

    /* Configure line: data bits, stop bits, parity */
    uint32_t lcr = 0;
    /* Data bits */
    switch (config.data_bits) {
        case DataBits::Bits7: lcr |= (2U << 0); break;
        case DataBits::Bits8: lcr |= LCR_DLS_8; break;
        default: lcr |= LCR_DLS_8; break;
    }
    /* Stop bits */
    if (config.stop_bits == StopBits::Two) lcr |= LCR_STOP_2;
    /* Parity */
    if (config.parity != Parity::None) {
        lcr |= LCR_PARITY_EN;
        if (config.parity == Parity::Even) lcr |= LCR_PARITY_EVEN;
        /* Odd parity: PARITY_ODD is 0, so no additional bit needed */
    }
    regs->LCR = lcr;

    /* Enable RX data available interrupt */
    regs->IER = IER_ERBFI;

    hal::Irq::enable(m_irq);

    m_initialized = true;
    return Status::Ok;
}

Status UartBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<Gr5525UartRegs *>(m_base);
    regs->IER = 0;
    m_initialized = false;
    return Status::Ok;
}

Status UartBase::send(const uint8_t *data, size_t len, size_t *bytes_sent, uint32_t timeout_ms) {
    if (!m_initialized || !data || len == 0) return Status::InvalidArgument;

    auto *regs = reinterpret_cast<Gr5525UartRegs *>(m_base);
    uint32_t timeout_loops = timeout_ms * (SystemCoreClock / 1000U / 4U);
    if (timeout_loops == 0) timeout_loops = 1;

    for (size_t i = 0; i < len; i++) {
        uint32_t remaining = timeout_loops;
        while (!(regs->LSR & LSR_THRE)) {
            if (--remaining == 0) return Status::Timeout;
        }
        regs->THR = data[i];
    }

    /* Wait for transmitter to actually drain */
    uint32_t remaining = timeout_loops;
    while (!(regs->LSR & LSR_TEMT)) {
        if (--remaining == 0) break;
    }

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
    auto *regs = reinterpret_cast<Gr5525UartRegs *>(m_base);
    if (regs->LSR & LSR_DR) {
        uint8_t ch = static_cast<uint8_t>(regs->RBR);
        m_rx_ring.write(&ch, 1);
        (void)m_rx_sem.release();
    }
}

} // namespace hal
