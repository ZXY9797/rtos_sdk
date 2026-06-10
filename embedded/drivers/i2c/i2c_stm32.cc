#include <drivers/i2c.h>
#include <assert.h>
#include <irq.h>
#include <osal.h>

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
constexpr uint32_t CR1_TCIE      = (1U << 6);   /* Transfer Complete IRQ */
constexpr uint32_t CR1_STOPIE    = (1U << 7);   /* Stop Detection IRQ */
constexpr uint32_t CR1_ERRIE     = (1U << 7);   /* Error IRQ (same bit as STOPIE on v2) */
constexpr uint32_t CR1_NACKIE    = (1U << 4);   /* NACK IRQ */
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
constexpr uint32_t ISR_BERR      = (1U << 8);
constexpr uint32_t ISR_BUSY      = (1U << 15);
// ICR
constexpr uint32_t ICR_NACKCF    = (1U << 4);
constexpr uint32_t ICR_STOPCF    = (1U << 5);
constexpr uint32_t ICR_BERRCF    = (1U << 8);

namespace {

/* I2C 实例 → I2cBase* 映射（用于 ISR 分发） */
constexpr int MAX_I2C_IRQ = 90;
static I2cBase *s_i2c_by_irq[MAX_I2C_IRQ] = {};

int i2c_irq_from_base(uintptr_t base) {
    switch (base) {
        case 0x40005400U: return I2C1_EV_IRQn;   /* I2C1 */
        case 0x40005800U: return I2C2_EV_IRQn;   /* I2C2 */
        case 0x40005C00U: return I2C3_EV_IRQn;   /* I2C3 */
        default: return -1;
    }
}

static void i2c_stm32_isr(int irq_num) {
    auto *i2c = s_i2c_by_irq[irq_num];
    if (!i2c) return;
    auto *regs = reinterpret_cast<I2cRegs *>(i2c->base());
    uint32_t isr = regs->ISR;
    uint32_t cr1 = regs->CR1;

    if (isr & ISR_NACKF) {
        regs->ICR = ICR_NACKCF | ICR_STOPCF;
        regs->CR1 = cr1 & ~(CR1_TCIE | CR1_STOPIE | CR1_NACKIE);
        i2c->stats().nack_addr_count++;
        (void)i2c->xfer_sem().release();
    } else if (isr & ISR_BERR) {
        regs->ICR = ICR_BERRCF;
        regs->CR1 = cr1 & ~(CR1_TCIE | CR1_STOPIE | CR1_NACKIE);
        i2c->stats().bus_error_count++;
        (void)i2c->xfer_sem().release();
    } else if ((isr & ISR_STOPF) || (isr & ISR_TC)) {
        regs->ICR = ICR_STOPCF;
        regs->CR1 = cr1 & ~(CR1_TCIE | CR1_STOPIE | CR1_NACKIE);
        (void)i2c->xfer_sem().release();
    }
}

} // anonymous namespace

Status I2cBase::init(uint32_t timing) {
    HAL_ASSERT(m_base != 0);
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);
    regs->CR1 = 0;
    regs->TIMINGR = timing;
    regs->CR1 = CR1_PE;

    /* 注册 ISR 映射并使能 I2C 事件中断 */
    int irq = i2c_irq_from_base(m_base);
    if (irq >= 0 && irq < MAX_I2C_IRQ) {
        s_i2c_by_irq[irq] = this;
        hal::Irq::enable(irq);
    }

    set_state(DeviceState::Initialized);
    return Status::Ok;
}

Status I2cBase::deinit() {
    if (!is_initialized()) return Status::Ok;
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);
    regs->CR1 = 0;
    set_state(DeviceState::Created);
    return Status::Ok;
}

Status I2cBase::master_transmit(uint16_t addr, const uint8_t *data, size_t len, uint32_t timeout_ms) {
    HAL_ASSERT_MSG(is_initialized(), "I2C not initialized");
    if (!is_initialized() || !data || len == 0) return Status::InvalidArgument;

    osal::LockGuard lock(m_bus_mutex);
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);

    regs->CR2 = (static_cast<uint32_t>(addr) << 1)
              | (static_cast<uint32_t>(len) << CR2_NBYTES_Pos)
              | CR2_START | CR2_AUTOEND;

    /* 使能中断：TC/STOP + NACK + 错误 */
    regs->CR1 |= CR1_STOPIE | CR1_NACKIE;

    /* 等待 ISR 释放信号量 */
    if (m_xfer_sem.take(timeout_ms) != 0) {
        regs->CR1 &= ~(CR1_TCIE | CR1_STOPIE | CR1_NACKIE);
        m_stats.timeout_count++;
        return Status::Timeout;
    }

    /* 检查是否因 NACK/BERR 而唤醒 */
    if (regs->ISR & (ISR_NACKF | ISR_BERR)) {
        regs->ICR = ICR_NACKCF | ICR_BERRCF | ICR_STOPCF;
        return Status::HardwareError;
    }

    regs->ICR = ICR_STOPCF;
    m_stats.transfer_count++;
    return Status::Ok;
}

Status I2cBase::master_receive(uint16_t addr, uint8_t *data, size_t len, uint32_t timeout_ms) {
    HAL_ASSERT_MSG(is_initialized(), "I2C not initialized");
    if (!is_initialized() || !data || len == 0) return Status::InvalidArgument;

    osal::LockGuard lock(m_bus_mutex);
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);

    regs->CR2 = (static_cast<uint32_t>(addr) << 1) | CR2_RD_WRN
              | (static_cast<uint32_t>(len) << CR2_NBYTES_Pos)
              | CR2_START | CR2_AUTOEND;

    regs->CR1 |= CR1_STOPIE | CR1_NACKIE;

    if (m_xfer_sem.take(timeout_ms) != 0) {
        regs->CR1 &= ~(CR1_TCIE | CR1_STOPIE | CR1_NACKIE);
        m_stats.timeout_count++;
        return Status::Timeout;
    }

    if (regs->ISR & (ISR_NACKF | ISR_BERR)) {
        regs->ICR = ICR_NACKCF | ICR_BERRCF | ICR_STOPCF;
        return Status::HardwareError;
    }

    /* 从 RXDR 读取接收到的数据 */
    for (size_t i = 0; i < len; i++) {
        data[i] = static_cast<uint8_t>(regs->RXDR);
    }

    regs->ICR = ICR_STOPCF;
    m_stats.transfer_count++;
    return Status::Ok;
}

Status I2cBase::mem_write(uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
                           const uint8_t *data, size_t len, uint32_t timeout_ms) {
    HAL_ASSERT_MSG(is_initialized(), "I2C not initialized");
    if (!is_initialized() || !data || len == 0) return Status::InvalidArgument;

    osal::LockGuard lock(m_bus_mutex);
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);

    size_t total = (addr_size == MemAddrSize::Bit16 ? 2 : 1) + len;
    regs->CR2 = (static_cast<uint32_t>(addr) << 1)
              | (static_cast<uint32_t>(total) << CR2_NBYTES_Pos)
              | CR2_START | CR2_AUTOEND;

    /* 写内存地址到 TXDR（硬件自动发送） */
    if (addr_size == MemAddrSize::Bit16) {
        regs->TXDR = static_cast<uint8_t>(mem_addr >> 8);
        /* 等待 TXIS 再写低字节 */
        uint32_t start_tick = osal::Kernel::tick_count();
        while (!(regs->ISR & ISR_TXIS)) {
            if (regs->ISR & ISR_NACKF) { regs->ICR = ICR_NACKCF; m_stats.nack_addr_count++; return Status::HardwareError; }
            if (regs->ISR & ISR_BERR)  { regs->ICR = ICR_BERRCF; m_stats.bus_error_count++; return Status::HardwareError; }
            if ((osal::Kernel::tick_count() - start_tick) >= timeout_ms) { m_stats.timeout_count++; return Status::Timeout; }
        }
        regs->TXDR = static_cast<uint8_t>(mem_addr & 0xFF);
    } else {
        regs->TXDR = static_cast<uint8_t>(mem_addr & 0xFF);
    }

    /* 写数据字节到 TXDR */
    for (size_t i = 0; i < len; i++) {
        uint32_t start_tick = osal::Kernel::tick_count();
        while (!(regs->ISR & ISR_TXIS)) {
            if (regs->ISR & ISR_NACKF) { regs->ICR = ICR_NACKCF | ICR_STOPCF; m_stats.nack_data_count++; return Status::HardwareError; }
            if (regs->ISR & ISR_BERR)  { regs->ICR = ICR_BERRCF; m_stats.bus_error_count++; return Status::HardwareError; }
            if ((osal::Kernel::tick_count() - start_tick) >= timeout_ms) { m_stats.timeout_count++; return Status::Timeout; }
        }
        regs->TXDR = data[i];
    }

    /* 等待 STOP 条件（AUTOEND 模式自动发送） */
    regs->CR1 |= CR1_STOPIE | CR1_NACKIE;
    if (m_xfer_sem.take(timeout_ms) != 0) {
        regs->CR1 &= ~(CR1_TCIE | CR1_STOPIE | CR1_NACKIE);
        m_stats.timeout_count++;
        return Status::Timeout;
    }

    if (regs->ISR & (ISR_NACKF | ISR_BERR)) {
        regs->ICR = ICR_NACKCF | ICR_BERRCF | ICR_STOPCF;
        return Status::HardwareError;
    }

    regs->ICR = ICR_STOPCF;
    m_stats.transfer_count++;
    return Status::Ok;
}

Status I2cBase::mem_read(uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
                          uint8_t *data, size_t len, uint32_t timeout_ms) {
    HAL_ASSERT_MSG(is_initialized(), "I2C not initialized");
    if (!is_initialized() || !data || len == 0) return Status::InvalidArgument;

    osal::LockGuard lock(m_bus_mutex);
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);

    // 写阶段：发送内存地址（非 AUTOEND，需手动发送 ReSTART）
    size_t addr_len = (addr_size == MemAddrSize::Bit16) ? 2 : 1;
    regs->CR2 = (static_cast<uint32_t>(addr) << 1)
              | (static_cast<uint32_t>(addr_len) << CR2_NBYTES_Pos)
              | CR2_START;

    if (addr_size == MemAddrSize::Bit16) {
        uint32_t start_tick = osal::Kernel::tick_count();
        while (!(regs->ISR & ISR_TXIS)) {
            if (regs->ISR & ISR_NACKF) { regs->ICR = ICR_NACKCF; m_stats.nack_addr_count++; return Status::HardwareError; }
            if (regs->ISR & ISR_BERR)  { regs->ICR = ICR_BERRCF; m_stats.bus_error_count++; return Status::HardwareError; }
            if ((osal::Kernel::tick_count() - start_tick) >= timeout_ms) { m_stats.timeout_count++; return Status::Timeout; }
        }
        regs->TXDR = static_cast<uint8_t>(mem_addr >> 8);
        while (!(regs->ISR & ISR_TXIS)) {
            if (regs->ISR & ISR_NACKF) { regs->ICR = ICR_NACKCF; m_stats.nack_addr_count++; return Status::HardwareError; }
            if (regs->ISR & ISR_BERR)  { regs->ICR = ICR_BERRCF; m_stats.bus_error_count++; return Status::HardwareError; }
            if ((osal::Kernel::tick_count() - start_tick) >= timeout_ms) { m_stats.timeout_count++; return Status::Timeout; }
        }
        regs->TXDR = static_cast<uint8_t>(mem_addr & 0xFF);
    } else {
        uint32_t start_tick = osal::Kernel::tick_count();
        while (!(regs->ISR & ISR_TXIS)) {
            if (regs->ISR & ISR_NACKF) { regs->ICR = ICR_NACKCF; m_stats.nack_addr_count++; return Status::HardwareError; }
            if (regs->ISR & ISR_BERR)  { regs->ICR = ICR_BERRCF; m_stats.bus_error_count++; return Status::HardwareError; }
            if ((osal::Kernel::tick_count() - start_tick) >= timeout_ms) { m_stats.timeout_count++; return Status::Timeout; }
        }
        regs->TXDR = static_cast<uint8_t>(mem_addr & 0xFF);
    }

    /* 等待 TC（地址发送完成） */
    {
        uint32_t start_tick = osal::Kernel::tick_count();
        while (!(regs->ISR & ISR_TC)) {
            if (regs->ISR & ISR_BERR) { regs->ICR = ICR_BERRCF; m_stats.bus_error_count++; return Status::HardwareError; }
            if ((osal::Kernel::tick_count() - start_tick) >= timeout_ms) { m_stats.timeout_count++; return Status::Timeout; }
        }
    }

    // 读阶段：ReSTART + 读数据（AUTOEND）
    regs->CR2 = (static_cast<uint32_t>(addr) << 1) | CR2_RD_WRN
              | (static_cast<uint32_t>(len) << CR2_NBYTES_Pos)
              | CR2_START | CR2_AUTOEND;

    regs->CR1 |= CR1_STOPIE | CR1_NACKIE;
    if (m_xfer_sem.take(timeout_ms) != 0) {
        regs->CR1 &= ~(CR1_TCIE | CR1_STOPIE | CR1_NACKIE);
        m_stats.timeout_count++;
        return Status::Timeout;
    }

    if (regs->ISR & (ISR_NACKF | ISR_BERR)) {
        regs->ICR = ICR_NACKCF | ICR_BERRCF | ICR_STOPCF;
        return Status::HardwareError;
    }

    for (size_t i = 0; i < len; i++) {
        data[i] = static_cast<uint8_t>(regs->RXDR);
    }

    regs->ICR = ICR_STOPCF;
    m_stats.transfer_count++;
    return Status::Ok;
}

Status I2cBase::is_ready(uint16_t addr, uint32_t retries, uint32_t timeout_ms) {
    if (!is_initialized()) return Status::InvalidArgument;

    osal::LockGuard lock(m_bus_mutex);
    auto *regs = reinterpret_cast<I2cRegs *>(m_base);

    for (uint32_t i = 0; i < retries; i++) {
        if (!(regs->ISR & ISR_BUSY)) {
            regs->CR2 = (static_cast<uint32_t>(addr) << 1) | CR2_START | CR2_STOP;
            uint32_t start_tick = osal::Kernel::tick_count();
            while (!(regs->ISR & (ISR_STOPF | ISR_NACKF))) {
                if ((osal::Kernel::tick_count() - start_tick) >= timeout_ms) break;
            }
            bool nack = (regs->ISR & ISR_NACKF) != 0;
            regs->ICR = ICR_NACKCF | ICR_STOPCF;
            if (!nack) return Status::Ok;
        }
    }
    return Status::Timeout;
}

} // namespace hal

/* I2C 事件中断服务函数 */
extern "C" void IRQ31_Handler(void) { hal::i2c_stm32_isr(31); }  /* I2C1_EV */
extern "C" void IRQ33_Handler(void) { hal::i2c_stm32_isr(33); }  /* I2C2_EV */
