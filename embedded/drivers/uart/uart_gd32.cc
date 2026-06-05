#include <drivers/uart.h>
#include <drivers_generated.h>
#include <arch/arch_interface.h>
#include <irq.h>

/* GD32F50x Register Definitions */
#include "gd32f50x.h"

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
#define USART_CTL0_PM_NONE      0x00000000U         /* No parity */
#define USART_CTL0_PM_EVEN      0x00000400U         /* Even parity */
#define USART_CTL0_PM_ODD       0x00000600U         /* Odd parity */
#define USART_CTL0_WL_8BIT      0x00000000U         /* 8 data bits */
#define USART_CTL0_WL_9BIT      0x00001000U         /* 9 data bits */

/* USART_CTL1 bits */
#define USART_CTL1_STB_MASK     BITS(13,12)         /* Stop bits mask */
#define USART_CTL1_STB_1BIT     0x00000000U         /* 1 stop bit */
#define USART_CTL1_STB_2BIT     0x00002000U         /* 2 stop bits */

/* RCU Register Definitions for USART clock enable */
#define RCU_APB1EN              REG32(RCU_BASE + 0x1CU)    /* APB1 enable register */
#define RCU_APB2EN              REG32(RCU_BASE + 0x18U)    /* APB2 enable register */

/* USART clock enable bits */
#define RCU_APB1EN_USART1EN     BIT(17)             /* USART1 clock enable */
#define RCU_APB1EN_USART2EN     BIT(18)             /* USART2 clock enable */
#define RCU_APB1EN_UART3EN      BIT(19)             /* UART3 clock enable */
#define RCU_APB1EN_UART4EN      BIT(20)             /* UART4 clock enable */
#define RCU_APB2EN_USART0EN     BIT(4)              /* USART0 clock enable */

namespace hal {

static void uart_isr_dispatch(const void *arg) {
    static_cast<UartBase *>(const_cast<void *>(arg))->isr_handler();
}

static void usart_clock_enable(uint32_t usart_periph) {
    switch (usart_periph) {
        case USART0_BASE:
            RCU_APB2EN |= RCU_APB2EN_USART0EN;
            break;
        case USART1_BASE:
            RCU_APB1EN |= RCU_APB1EN_USART1EN;
            break;
        case USART2_BASE:
            RCU_APB1EN |= RCU_APB1EN_USART2EN;
            break;
        case UART3_BASE:
            RCU_APB1EN |= RCU_APB1EN_UART3EN;
            break;
        case UART4_BASE:
            RCU_APB1EN |= RCU_APB1EN_UART4EN;
            break;
        default:
            break;
    }
}

Status UartBase::init(const UartConfig &config) {
    if (!config.rx_buffer || config.rx_buffer_size == 0) return Status::InvalidArgument;

    m_rx_ring = RingBuf(config.rx_buffer, config.rx_buffer_size);

    uint32_t usart_periph = static_cast<uint32_t>(m_base);

    /* Enable USART clock */
    usart_clock_enable(usart_periph);

    /* Disable USART */
    USART_CTL0(usart_periph) &= ~USART_CTL0_UEN;

    /* Configure baud rate: USARTDIV = PCLK / (16 * baudrate) */
    uint32_t pclk = (usart_periph == USART0_BASE) ? SystemCoreClock : (SystemCoreClock / 2);
    uint32_t baud_div = (pclk + config.baudrate / 2) / config.baudrate;
    USART_BAUD(usart_periph) = baud_div;

    /* Configure data bits */
    USART_CTL0(usart_periph) &= ~USART_CTL0_WL_9BIT;
    if (config.data_bits == DataBits::Bits9) {
        USART_CTL0(usart_periph) |= USART_CTL0_WL_9BIT;
    }

    /* Configure stop bits */
    USART_CTL1(usart_periph) &= ~USART_CTL1_STB_MASK;
    if (config.stop_bits == StopBits::Two) {
        USART_CTL1(usart_periph) |= USART_CTL1_STB_2BIT;
    }

    /* Configure parity */
    USART_CTL0(usart_periph) &= ~(USART_CTL0_PM_ODD);
    switch (config.parity) {
        case Parity::Odd:
            USART_CTL0(usart_periph) |= USART_CTL0_PM_ODD;
            break;
        case Parity::Even:
            USART_CTL0(usart_periph) |= USART_CTL0_PM_EVEN;
            break;
        default:
            break;
    }

    /* Enable transmitter and receiver */
    USART_CTL0(usart_periph) |= USART_CTL0_TEN | USART_CTL0_REN;

    /* Enable receive interrupt */
    USART_CTL0(usart_periph) |= USART_CTL0_RBNEIE;

    /* Enable USART */
    USART_CTL0(usart_periph) |= USART_CTL0_UEN;

    /* Connect interrupt */
    arch_irq_connect_dynamic(m_irq, 0, uart_isr_dispatch, this, 0);
    arch_irq_enable(m_irq);

    m_initialized = true;
    return Status::Ok;
}

Status UartBase::deinit() {
    if (!m_initialized) return Status::Ok;

    uint32_t usart_periph = static_cast<uint32_t>(m_base);
    USART_CTL0(usart_periph) &= ~USART_CTL0_UEN;
    m_initialized = false;
    return Status::Ok;
}

Status UartBase::send(const uint8_t *data, size_t len, size_t *bytes_sent, uint32_t timeout_ms) {
    if (!m_initialized || !data || len == 0) return Status::InvalidArgument;
    (void)timeout_ms;

    uint32_t usart_periph = static_cast<uint32_t>(m_base);

    for (size_t i = 0; i < len; i++) {
        while (!(USART_STAT(usart_periph) & USART_STAT_TBE)) {}
        USART_DATA(usart_periph) = data[i];
    }
    while (!(USART_STAT(usart_periph) & USART_STAT_TC)) {}

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
    uint32_t usart_periph = static_cast<uint32_t>(m_base);

    if (USART_STAT(usart_periph) & USART_STAT_RBNE) {
        uint8_t ch = static_cast<uint8_t>(USART_DATA(usart_periph));
        m_rx_ring.write(&ch, 1);
        (void)m_rx_sem.release();
    }
}

} // namespace hal
