#include <drivers/i2c.h>

namespace hal {

// I2C 寄存器结构体（映射 STM32H7 I2C v2）
struct I2cRegs {
    volatile uint32_t CR1;       // 0x00
    volatile uint32_t CR2;       // 0x04
    volatile uint32_t OAR1;      // 0x08
    volatile uint32_t OAR2;      // 0x0C
    volatile uint32_t TIMINGR;   // 0x10
    volatile uint32_t TIMEOUTR;  // 0x14
    volatile uint32_t ISR;       // 0x18
    volatile uint32_t ICR;       // 0x1C
    volatile uint32_t PECR;      // 0x20
    volatile uint32_t RXDR;      // 0x24
    volatile uint32_t TXDR;      // 0x28
};

// CR1
constexpr uint32_t CR1_PE        = (1U << 0);
// CR2
constexpr uint32_t CR2_SADD_Pos  = 0;
constexpr uint32_t CR2_RD_WRN    = (1U << 10);
constexpr uint32_t CR2_START     = (1U << 13);
constexpr uint32_t CR2_STOP      = (1U << 14);
constexpr uint32_t CR2_NACK      = (1U << 15);
constexpr uint32_t CR2_NBYTES_Pos = 16;
constexpr uint32_t CR2_AUTOEND   = (1U << 25);
// ISR
constexpr uint32_t ISR_TXE       = (1U << 0);
constexpr uint32_t ISR_TXIS      = (1U << 1);
constexpr uint32_t ISR_RXNE      = (1U << 2);
constexpr uint32_t ISR_STOPF     = (1U << 5);
constexpr uint32_t ISR_TC        = (1U << 6);
constexpr uint32_t ISR_NACKF     = (1U << 4);
constexpr uint32_t ISR_BUSY      = (1U << 15);
// ICR
constexpr uint32_t ICR_NACKCF    = (1U << 4);
constexpr uint32_t ICR_STOPCF    = (1U << 5);
constexpr uint32_t ICR_BERRCF    = (1U << 8);

Status I2cBase::init(uint32_t timing) {
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);
    regs->CR1 = 0;
    regs->TIMINGR = timing;
    regs->CR1 = CR1_PE;
    m_initialized = true;
    return Status::Ok;
}

Status I2cBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);
    regs->CR1 = 0;
    m_initialized = false;
    return Status::Ok;
}

Status I2cBase::master_transmit(uint16_t addr, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (!m_initialized || !data || len == 0) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);
    (void)timeout_ms;

    regs->CR2 = (static_cast<uint32_t>(addr) << 1)
              | (static_cast<uint32_t>(len) << CR2_NBYTES_Pos)
              | CR2_START | CR2_AUTOEND;

    for (size_t i = 0; i < len; i++) {
        while (!(regs->ISR & ISR_TXIS)) {
            if (regs->ISR & ISR_NACKF) { regs->ICR = ICR_NACKCF; return Status::HardwareError; }
        }
        regs->TXDR = data[i];
    }

    while (!(regs->ISR & ISR_STOPF)) {}
    regs->ICR = ICR_STOPCF;
    return Status::Ok;
}

Status I2cBase::master_receive(uint16_t addr, uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (!m_initialized || !data || len == 0) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);
    (void)timeout_ms;

    regs->CR2 = (static_cast<uint32_t>(addr) << 1) | CR2_RD_WRN
              | (static_cast<uint32_t>(len) << CR2_NBYTES_Pos)
              | CR2_START | CR2_AUTOEND;

    for (size_t i = 0; i < len; i++) {
        while (!(regs->ISR & ISR_RXNE)) {
            if (regs->ISR & ISR_NACKF) { regs->ICR = ICR_NACKCF; return Status::HardwareError; }
        }
        data[i] = static_cast<uint8_t>(regs->RXDR);
    }

    while (!(regs->ISR & ISR_STOPF)) {}
    regs->ICR = ICR_STOPCF;
    return Status::Ok;
}

Status I2cBase::mem_write(uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
                           const uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (!m_initialized || !data || len == 0) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);
    (void)timeout_ms;

    size_t total = (addr_size == MemAddrSize::Bit16 ? 2 : 1) + len;
    regs->CR2 = (static_cast<uint32_t>(addr) << 1)
              | (static_cast<uint32_t>(total) << CR2_NBYTES_Pos)
              | CR2_START | CR2_AUTOEND;

    if (addr_size == MemAddrSize::Bit16) {
        while (!(regs->ISR & ISR_TXIS)) {}
        regs->TXDR = static_cast<uint8_t>(mem_addr >> 8);
        while (!(regs->ISR & ISR_TXIS)) {}
        regs->TXDR = static_cast<uint8_t>(mem_addr & 0xFF);
    } else {
        while (!(regs->ISR & ISR_TXIS)) {}
        regs->TXDR = static_cast<uint8_t>(mem_addr & 0xFF);
    }

    for (size_t i = 0; i < len; i++) {
        while (!(regs->ISR & ISR_TXIS)) {
            if (regs->ISR & ISR_NACKF) { regs->ICR = ICR_NACKCF | ICR_STOPCF; return Status::HardwareError; }
        }
        regs->TXDR = data[i];
    }

    while (!(regs->ISR & ISR_STOPF)) {}
    regs->ICR = ICR_STOPCF;
    return Status::Ok;
}

Status I2cBase::mem_read(uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
                          uint8_t *data, size_t len, uint32_t timeout_ms) {
    if (!m_initialized || !data || len == 0) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);
    (void)timeout_ms;

    // 写阶段：发送内存地址
    size_t addr_len = (addr_size == MemAddrSize::Bit16) ? 2 : 1;
    regs->CR2 = (static_cast<uint32_t>(addr) << 1)
              | (static_cast<uint32_t>(addr_len) << CR2_NBYTES_Pos)
              | CR2_START;

    if (addr_size == MemAddrSize::Bit16) {
        while (!(regs->ISR & ISR_TXIS)) {}
        regs->TXDR = static_cast<uint8_t>(mem_addr >> 8);
        while (!(regs->ISR & ISR_TXIS)) {}
        regs->TXDR = static_cast<uint8_t>(mem_addr & 0xFF);
    } else {
        while (!(regs->ISR & ISR_TXIS)) {}
        regs->TXDR = static_cast<uint8_t>(mem_addr & 0xFF);
    }
    while (!(regs->ISR & ISR_TC)) {}

    // 读阶段
    regs->CR2 = (static_cast<uint32_t>(addr) << 1) | CR2_RD_WRN
              | (static_cast<uint32_t>(len) << CR2_NBYTES_Pos)
              | CR2_START | CR2_AUTOEND;

    for (size_t i = 0; i < len; i++) {
        while (!(regs->ISR & ISR_RXNE)) {}
        data[i] = static_cast<uint8_t>(regs->RXDR);
    }

    while (!(regs->ISR & ISR_STOPF)) {}
    regs->ICR = ICR_STOPCF;
    return Status::Ok;
}

Status I2cBase::is_ready(uint16_t addr, uint32_t retries, uint32_t timeout_ms) {
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);
    (void)timeout_ms;

    for (uint32_t i = 0; i < retries; i++) {
        if (!(regs->ISR & ISR_BUSY)) {
            regs->CR2 = (static_cast<uint32_t>(addr) << 1) | CR2_START | CR2_STOP;
            while (!(regs->ISR & (ISR_STOPF | ISR_NACKF))) {}
            bool nack = (regs->ISR & ISR_NACKF) != 0;
            regs->ICR = ICR_NACKCF | ICR_STOPCF;
            if (!nack) return Status::Ok;
        }
    }
    return Status::Timeout;
}

} // namespace hal
