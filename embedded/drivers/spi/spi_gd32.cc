#include <drivers/spi.h>
#include <drivers/dma.h>
#include <arch/arch_interface.h>

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

} // anonymous namespace

Status SpiBase::init(const SpiConfig &config) {
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

    m_initialized = true;
    return Status::Ok;
}

Status SpiBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<gd32::SpiRegs *>(m_base);
    regs->CTL0 &= ~CTL0_SPIEN;
    m_initialized = false;
    return Status::Ok;
}

Status SpiBase::sync_send(const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms) {
    if (!m_initialized || len == 0) return Status::InvalidArgument;

    auto *regs = reinterpret_cast<gd32::SpiRegs *>(m_base);
    uint32_t timeout_loops = timeout_ms * (SystemCoreClock / 1000U / 4U);
    if (timeout_loops == 0) timeout_loops = 1;

    /* DMA channel mapping per SPI peripheral */
    struct DmaMapping { uint32_t tx_req, rx_req, tx_dma, rx_dma; uint8_t tx_ch, rx_ch, tx_mux, rx_mux; };
    DmaMapping m;
    switch (static_cast<uint32_t>(m_base)) {
        case SPI0_BASE: m = {DMA_REQUEST_SPI0_TX, DMA_REQUEST_SPI0_RX, DMA0_BASE, DMA0_BASE, 3, 4, 3, 4}; break;
        case SPI1_BASE: m = {DMA_REQUEST_SPI1_TX, DMA_REQUEST_SPI1_RX, DMA0_BASE, DMA0_BASE, 5, 6, 5, 6}; break;
        case SPI2_BASE: m = {DMA_REQUEST_SPI2_TX, DMA_REQUEST_SPI2_RX, DMA1_BASE, DMA1_BASE, 0, 1, 0, 1}; break;
        default: return Status::InvalidArgument;
    }

    DmaChannel tx_dma(m.tx_dma, m.tx_ch, m.tx_mux);
    DmaChannel rx_dma(m.rx_dma, m.rx_ch, m.rx_mux);
    tx_dma.clear_flags();
    rx_dma.clear_flags();

    static const uint8_t dummy_tx = 0xFF;
    const uint8_t *tx_buf = tx ? tx : &dummy_tx;

    DmaConfig rx_cfg = {
        .request_id = m.rx_req, .periph_addr = reinterpret_cast<uint32_t>(&regs->DATA),
        .memory_addr = reinterpret_cast<uint32_t>(rx), .count = static_cast<uint16_t>(len),
        .direction = DmaDirection::PeriphToMemory, .periph_width = DmaWidth::Byte,
        .memory_width = DmaWidth::Byte, .priority = DmaPriority::High,
        .periph_inc = false, .memory_inc = rx != nullptr, .circular = false,
    };
    DmaConfig tx_cfg = {
        .request_id = m.tx_req, .periph_addr = reinterpret_cast<uint32_t>(&regs->DATA),
        .memory_addr = reinterpret_cast<uint32_t>(tx_buf), .count = static_cast<uint16_t>(len),
        .direction = DmaDirection::MemoryToPeriph, .periph_width = DmaWidth::Byte,
        .memory_width = DmaWidth::Byte, .priority = DmaPriority::High,
        .periph_inc = tx != nullptr, .memory_inc = true, .circular = false,
    };

    rx_dma.config(rx_cfg);
    tx_dma.config(tx_cfg);
    rx_dma.start();
    tx_dma.start();

    uint32_t remaining = timeout_loops;
    while (!tx_dma.is_transfer_complete()) { if (--remaining == 0) return Status::Timeout; }
    remaining = timeout_loops;
    while (!rx_dma.is_transfer_complete()) { if (--remaining == 0) return Status::Timeout; }
    remaining = timeout_loops;
    while (regs->STAT & STAT_TRANS) { if (--remaining == 0) return Status::Timeout; }

    tx_dma.stop(); rx_dma.stop();
    tx_dma.clear_flags(); rx_dma.clear_flags();

    return Status::Ok;
}

} // namespace hal
