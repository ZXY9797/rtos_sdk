#include <drivers/spi.h>
#include <drivers/dma.h>
#include <assert.h>
#include <osal.h>

#include "gd32_regs.h"
#include "gd32f50x_dma.h"

namespace hal {
namespace {

/* CTL0 bits */
constexpr uint32_t CTL0_CKPH    = (1U << 0);
constexpr uint32_t CTL0_CKPL    = (1U << 1);
constexpr uint32_t CTL0_MSTMOD  = (1U << 2);
constexpr uint32_t CTL0_PSC_Msk = (7U << 3);
constexpr uint32_t CTL0_SPIEN   = (1U << 6);
constexpr uint32_t CTL0_SWNSS   = (1U << 8);
constexpr uint32_t CTL0_SWNSSEN = (1U << 9);
constexpr uint32_t CTL0_FF16    = (1U << 11);

/* CTL1 bits */
constexpr uint32_t CTL1_DMAREN = (1U << 0);
constexpr uint32_t CTL1_DMATEN = (1U << 1);

/* STAT bits */
constexpr uint32_t STAT_TRANS = (1U << 7);

void spi_clock_enable(uintptr_t base) {
    auto &apb1 = gd32::rcu_apb1en();
    auto &apb2 = gd32::rcu_apb2en();
    switch (base) {
        case SPI0_BASE: apb2 |= gd32::clk::SPI0EN; break;
        case SPI1_BASE: apb1 |= gd32::clk::SPI1EN; break;
        case SPI2_BASE: apb1 |= gd32::clk::SPI2EN; break;
        default: break;
    }
}

void dma_channel_isr(const DmaChannelConfig &config,
                     osal::Semaphore &semaphore,
                     osal::IsrContext& context)
{
    if (!config.is_valid() || config.channel > 6U) {
        return;
    }

    auto *dma = reinterpret_cast<gd32::DmaRegs *>(config.controller);
    dma->INTC = (DMA_INTC_GIFC | DMA_INTC_FTFIFC
                 | DMA_INTC_HTFIFC | DMA_INTC_ERRIFC)
                << (config.channel * 4U);
    (void)semaphore.release_from_isr(context);
}

} // anonymous namespace

Status SpiBase::init(const SpiConfig &config) {
    HAL_ASSERT(m_base != 0);
    if (!config.dma_tx.is_valid() || !config.dma_rx.is_valid() ||
        config.dma_tx.channel > 6U || config.dma_rx.channel > 6U ||
        (config.dma_tx.controller == config.dma_rx.controller &&
         config.dma_tx.channel == config.dma_rx.channel)) {
        return Status::InvalidArgument;
    }

    m_dma_tx = config.dma_tx;
    m_dma_rx = config.dma_rx;

    auto *regs = reinterpret_cast<gd32::SpiRegs *>(m_base);
    spi_clock_enable(m_base);

    regs->CTL0 &= ~CTL0_SPIEN;

    uint32_t ctl0 = CTL0_MSTMOD | CTL0_SWNSS | CTL0_SWNSSEN;

    if (config.mode == SpiMode::Mode1 || config.mode == SpiMode::Mode3) ctl0 |= CTL0_CKPH;
    if (config.mode == SpiMode::Mode2 || config.mode == SpiMode::Mode3) ctl0 |= CTL0_CKPL;
    if (config.data_bits == 16) ctl0 |= CTL0_FF16;

    uint32_t pclk = (m_base == SPI0_BASE) ? SystemCoreClock : (SystemCoreClock / 2);
    uint32_t psc = 0;
    uint32_t div = 2;
    while (div < 256 && (pclk / div) > config.clock_hz) { psc++; div <<= 1; }
    if (psc > 7) psc = 7;
    ctl0 |= (psc << 3U) & CTL0_PSC_Msk;

    regs->CTL0 = ctl0;
    regs->CTL1 |= CTL1_DMATEN | CTL1_DMAREN;
    regs->CTL0 |= CTL0_SPIEN;

    set_state(DeviceState::Initialized);
    return Status::Ok;
}

Status SpiBase::deinit() {
    if (!is_initialized()) return Status::Ok;
    auto *regs = reinterpret_cast<gd32::SpiRegs *>(m_base);
    regs->CTL0 &= ~CTL0_SPIEN;
    set_state(DeviceState::Created);
    return Status::Ok;
}

Status SpiBase::sync_send(const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms) {
    HAL_ASSERT_MSG(is_initialized(), "SPI not initialized");
    if (!is_initialized() || len == 0 || len > 0xFFFFU) {
        return Status::InvalidArgument;
    }

    osal::LockGuard lock(m_bus_mutex);

    auto *regs = reinterpret_cast<gd32::SpiRegs *>(m_base);

    DmaChannel tx_dma(static_cast<uint32_t>(m_dma_tx.controller),
                      m_dma_tx.channel, m_dma_tx.mux_channel);
    DmaChannel rx_dma(static_cast<uint32_t>(m_dma_rx.controller),
                      m_dma_rx.channel, m_dma_rx.mux_channel);
    tx_dma.clear_flags();
    rx_dma.clear_flags();

    const uint8_t dummy_tx = 0xFF;
    uint8_t dummy_rx = 0U;
    const uint8_t *tx_buf = tx ? tx : &dummy_tx;
    uint8_t *rx_buf = rx ? rx : &dummy_rx;

    DmaConfig rx_cfg = {
        .request_id = m_dma_rx.request_id,
        .periph_addr = reinterpret_cast<uint32_t>(&regs->DATA),
        .memory_addr = reinterpret_cast<uint32_t>(rx_buf),
        .count = static_cast<uint16_t>(len),
        .direction = DmaDirection::PeriphToMemory, .periph_width = DmaWidth::Byte,
        .memory_width = DmaWidth::Byte, .priority = DmaPriority::High,
        .periph_inc = false, .memory_inc = rx != nullptr, .circular = false,
    };
    DmaConfig tx_cfg = {
        .request_id = m_dma_tx.request_id,
        .periph_addr = reinterpret_cast<uint32_t>(&regs->DATA),
        .memory_addr = reinterpret_cast<uint32_t>(tx_buf), .count = static_cast<uint16_t>(len),
        .direction = DmaDirection::MemoryToPeriph, .periph_width = DmaWidth::Byte,
        .memory_width = DmaWidth::Byte, .priority = DmaPriority::High,
        .periph_inc = false, .memory_inc = tx != nullptr, .circular = false,
    };

    Status dma_status = rx_dma.config(rx_cfg);
    if (dma_status != Status::Ok) {
        m_stats.error_count++;
        return dma_status;
    }
    dma_status = tx_dma.config(tx_cfg);
    if (dma_status != Status::Ok) {
        m_stats.error_count++;
        return dma_status;
    }
    dma_status = rx_dma.start();
    if (dma_status != Status::Ok) {
        m_stats.error_count++;
        return dma_status;
    }
    dma_status = tx_dma.start();
    if (dma_status != Status::Ok) {
        (void)rx_dma.stop();
        m_stats.error_count++;
        return dma_status;
    }

    /* 等待 TX DMA 完成（ISR 释放信号量） */
    if (m_xfer_sem.take(timeout_ms) != 0) {
        m_stats.timeout_count++;
        (void)tx_dma.stop(); (void)rx_dma.stop();
        tx_dma.clear_flags(); rx_dma.clear_flags();
        return Status::Timeout;
    }
    /* 等待 RX DMA 完成 */
    if (m_xfer_sem.take(timeout_ms) != 0) {
        m_stats.timeout_count++;
        (void)tx_dma.stop(); (void)rx_dma.stop();
        tx_dma.clear_flags(); rx_dma.clear_flags();
        return Status::Timeout;
    }

    /* 等待 SPI 总线空闲 */
    uint32_t start_tick = osal::Kernel::tick_count();
    while (regs->STAT & STAT_TRANS) {
        if ((osal::Kernel::tick_count() - start_tick) >= timeout_ms) {
            m_stats.timeout_count++;
            return Status::Timeout;
        }
    }

    m_stats.xfer_count++;
    m_stats.xfer_bytes += len;
    return Status::Ok;
}

void SpiBase::dma_tx_isr(osal::IsrContext& context)
{
    dma_channel_isr(m_dma_tx, m_xfer_sem, context);
}

void SpiBase::dma_rx_isr(osal::IsrContext& context)
{
    dma_channel_isr(m_dma_rx, m_xfer_sem, context);
}

} // namespace hal
