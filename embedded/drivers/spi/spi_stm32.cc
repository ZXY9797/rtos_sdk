#include <drivers/spi.h>

namespace hal {

// SPI 寄存器结构体（映射 STM32H7 增强 SPI）
struct SpiRegs {
    volatile uint32_t CR1;       // 0x00
    volatile uint32_t CR2;       // 0x04
    volatile uint32_t CFG1;      // 0x08
    volatile uint32_t CFG2;      // 0x0C
    volatile uint32_t IER;       // 0x10
    volatile uint32_t SR;        // 0x14
    volatile uint32_t IFCR;      // 0x18
    volatile uint32_t RESERVED0; // 0x1C
    volatile uint32_t TXDR;      // 0x20
    volatile uint32_t RESERVED1[3];
    volatile uint32_t RXDR;      // 0x30
};

// CR1
constexpr uint32_t CR1_SPE    = (1U << 0);
constexpr uint32_t CR1_CSTART = (1U << 9);
// CFG1
constexpr uint32_t CFG1_DSIZE_Pos = 0;
constexpr uint32_t CFG1_FTHLV_Pos = 5;
// CFG2
constexpr uint32_t CFG2_MSTR  = (1U << 22);
constexpr uint32_t CFG2_LSBFRST = (1U << 23);
constexpr uint32_t CFG2_CPHA  = (1U << 24);
constexpr uint32_t CFG2_CPOL  = (1U << 25);
constexpr uint32_t CFG2_SSM   = (1U << 26);
// SR
constexpr uint32_t SR_RXP  = (1U << 0);
constexpr uint32_t SR_TXP  = (1U << 1);
constexpr uint32_t SR_EOT  = (1U << 3);
constexpr uint32_t SR_OVR  = (1U << 6);
constexpr uint32_t SR_TXC  = (1U << 12);
// IFCR
constexpr uint32_t IFCR_EOTC  = (1U << 3);
constexpr uint32_t IFCR_TXTFC = (1U << 4);
constexpr uint32_t IFCR_OVRC  = (1U << 6);

Status SpiBase::init(const SpiConfig &config) {
    auto *regs = reinterpret_cast<SpiRegs *>(m_base);

    regs->CR1 = 0;
    regs->CFG2 = CFG2_MSTR | CFG2_SSM;

    switch (config.mode) {
    case SpiMode::Mode0: break;
    case SpiMode::Mode1: regs->CFG2 |= CFG2_CPHA; break;
    case SpiMode::Mode2: regs->CFG2 |= CFG2_CPOL; break;
    case SpiMode::Mode3: regs->CFG2 |= CFG2_CPOL | CFG2_CPHA; break;
    }

    regs->CFG1 = ((config.data_bits - 1) & 0x1FU) << CFG1_DSIZE_Pos;
    regs->CR1 = CR1_SPE;
    m_initialized = true;
    return Status::Ok;
}

Status SpiBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<SpiRegs *>(m_base);
    regs->CR1 = 0;
    m_initialized = false;
    return Status::Ok;
}

Status SpiBase::sync_send(const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<SpiRegs *>(m_base);
    (void)timeout_ms;

    regs->IFCR = IFCR_EOTC | IFCR_TXTFC | IFCR_OVRC;
    regs->CR1 |= CR1_CSTART;

    for (size_t i = 0; i < len; i++) {
        while (!(regs->SR & SR_TXP)) {}
        regs->TXDR = tx ? tx[i] : 0xFF;
        while (!(regs->SR & SR_RXP)) {}
        uint8_t data = static_cast<uint8_t>(regs->RXDR);
        if (rx) rx[i] = data;
    }

    while (!(regs->SR & SR_EOT)) {}
    regs->IFCR = IFCR_EOTC;
    return Status::Ok;
}

} // namespace hal
