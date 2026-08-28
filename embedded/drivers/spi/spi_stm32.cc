#include <drivers/spi.h>
#include <assert.h>
#include <osal.h>

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
constexpr uint32_t CR2_TSIZE_Msk = 0xFFFFU;
// CFG1
constexpr uint32_t CFG1_DSIZE_Pos = 0;
constexpr uint32_t CFG1_FTHLV_Pos = 5;
// CFG2
constexpr uint32_t CFG2_MSTR  = (1U << 22);
constexpr uint32_t CFG2_LSBFRST = (1U << 23);
constexpr uint32_t CFG2_CPHA  = (1U << 24);
constexpr uint32_t CFG2_CPOL  = (1U << 25);
constexpr uint32_t CFG2_SSM   = (1U << 26);
// IER
constexpr uint32_t IER_EOTIE  = (1U << 3);
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
    HAL_ASSERT(m_base != 0);
    if (config.clock_hz == 0U
        || config.data_bits != 8U) {
        return Status::InvalidArgument;
    }
    if (!m_bus_mutex.is_valid() || !m_xfer_sem.is_valid()) {
        return Status::NoMemory;
    }
    m_config = config;
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

    set_state(DeviceState::Initialized);
    return Status::Ok;
}

Status SpiBase::deinit() {
    if (!is_initialized()) return Status::Ok;
    auto *regs = reinterpret_cast<SpiRegs *>(m_base);
    regs->CR1 = 0;
    set_state(DeviceState::Created);
    return Status::Ok;
}

void SpiBase::isr_handler(osal::IsrContext& context)
{
    auto *regs = reinterpret_cast<SpiRegs *>(m_base);
    uint32_t sr = regs->SR;
    if (sr & SR_EOT) {
        regs->IFCR = IFCR_EOTC;
        regs->IER &= ~IER_EOTIE;
        (void)m_xfer_sem.release_from_isr(context);
    }
}

Status SpiBase::transfer(const uint8_t *tx, uint8_t *rx, size_t len,
                         uint32_t timeout_ms, ChipSelect chip_select) {
    HAL_ASSERT_MSG(is_initialized(), "SPI not initialized");
    if (!is_initialized() || len == 0U || len > CR2_TSIZE_Msk) {
        return Status::InvalidArgument;
    }

    osal::Deadline deadline(timeout_ms);
    osal::LockGuard lock(m_bus_mutex, deadline.remaining());
    if (!lock.owns_lock()) {
        return Status::Busy;
    }
    while (m_xfer_sem.take(0U) == 0) {
    }
    detail::SpiChipSelectGuard select_guard(
        chip_select.function, chip_select.argument);
    auto *regs = reinterpret_cast<SpiRegs *>(m_base);

    regs->IFCR = IFCR_EOTC | IFCR_TXTFC | IFCR_OVRC;
    regs->CR2 = (regs->CR2 & ~CR2_TSIZE_Msk)
        | static_cast<uint32_t>(len);
    regs->CR1 |= CR1_CSTART;

    /* 数据传输阶段：逐字节轮询（SPI 时钟 MHz 级，单字节延迟 <1μs） */
    for (size_t i = 0; i < len; i++) {
        while (!(regs->SR & SR_TXP)) {
            if (deadline.expired()) {
                m_stats.timeout_count++;
                return Status::Timeout;
            }
        }
        regs->TXDR = tx ? tx[i] : 0xFF;
        while (!(regs->SR & SR_RXP)) {
            if (deadline.expired()) {
                m_stats.timeout_count++;
                return Status::Timeout;
            }
        }
        uint8_t data = static_cast<uint8_t>(regs->RXDR);
        if (rx) rx[i] = data;
    }

    /* 最终等待：EOT 中断 + 信号量让出 CPU */
    regs->IER |= IER_EOTIE;
    if (m_xfer_sem.take(deadline.remaining()) != 0) {
        regs->IER &= ~IER_EOTIE;
        m_stats.timeout_count++;
        return Status::Timeout;
    }

    m_stats.xfer_count++;
    m_stats.xfer_bytes += len;
    return Status::Ok;
}

Status SpiBase::begin_async(AsyncCallback, void*)
{
    return Status::NotSupported;
}

Status SpiBase::transfer_async(
    const uint8_t*, uint8_t*, size_t, ChipSelect)
{
    return Status::NotSupported;
}

Status SpiBase::end_async()
{
    return Status::Ok;
}

} // namespace hal
