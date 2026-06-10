#include <drivers/spi.h>
#include <drivers/dma.h>
#include <assert.h>
#include <irq.h>
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

/* DMA 中断标志位偏移（每个通道占 4 位） */
constexpr uint32_t DMA_FLAG_SHIFT = 4U;
constexpr uint32_t DMA_FTF_FLAG   = (1U << 1);  /* FTFIF 在 INTF 中的位置 */

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

/* DMA 通道 → SPI 实例 映射（用于 ISR 分发） */
struct DmaSpiMapping {
    SpiBase *spi;
    uint32_t dma_base;
};

/* 最大支持的 DMA IRQ 号 + 1（GD32F503 最大 IRQ 85） */
constexpr int MAX_DMA_IRQ = 90;
static DmaSpiMapping s_dma_spi_map[MAX_DMA_IRQ] = {};

static void dma_channel_isr(int irq_num) {
    auto &m = s_dma_spi_map[irq_num];
    if (!m.spi) return;
    auto *dma = reinterpret_cast<gd32::DmaRegs *>(m.dma_base);
    /* 清除所有中断标志（通过 INTC 写 1 清除） */
    dma->INTC = (DMA_INTC_GIFC | DMA_INTC_FTFIFC
                 | DMA_INTC_HTFIFC | DMA_INTC_ERRIFC);
    (void)m.spi->xfer_sem().release();
}

} // anonymous namespace

Status SpiBase::init(const SpiConfig &config) {
    HAL_ASSERT(m_base != 0);
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

    /* 注册 DMA 通道中断映射并使能中断 */
    switch (static_cast<uint32_t>(m_base)) {
        case SPI0_BASE:
            s_dma_spi_map[DMA0_Channel3_IRQn] = {this, DMA0_BASE};
            s_dma_spi_map[DMA0_Channel4_IRQn] = {this, DMA0_BASE};
            hal::Irq::enable(DMA0_Channel3_IRQn);
            hal::Irq::enable(DMA0_Channel4_IRQn);
            break;
        case SPI1_BASE:
            s_dma_spi_map[DMA0_Channel5_IRQn] = {this, DMA0_BASE};
            s_dma_spi_map[DMA0_Channel6_IRQn] = {this, DMA0_BASE};
            hal::Irq::enable(DMA0_Channel5_IRQn);
            hal::Irq::enable(DMA0_Channel6_IRQn);
            break;
        case SPI2_BASE:
            s_dma_spi_map[DMA1_Channel0_IRQn] = {this, DMA1_BASE};
            s_dma_spi_map[DMA1_Channel1_IRQn] = {this, DMA1_BASE};
            hal::Irq::enable(DMA1_Channel0_IRQn);
            hal::Irq::enable(DMA1_Channel1_IRQn);
            break;
        default: break;
    }

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
    if (!is_initialized() || len == 0) return Status::InvalidArgument;

    osal::LockGuard lock(m_bus_mutex);

    auto *regs = reinterpret_cast<gd32::SpiRegs *>(m_base);

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

    (void)rx_dma.config(rx_cfg);
    (void)tx_dma.config(tx_cfg);
    (void)rx_dma.start();
    (void)tx_dma.start();

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

} // namespace hal

/* DMA 中断服务函数 — 直接覆盖向量表弱别名 */
extern "C" void IRQ14_Handler(void) { hal::dma_channel_isr(14); }  /* DMA0_CH3 */
extern "C" void IRQ15_Handler(void) { hal::dma_channel_isr(15); }  /* DMA0_CH4 */
extern "C" void IRQ16_Handler(void) { hal::dma_channel_isr(16); }  /* DMA0_CH5 */
extern "C" void IRQ17_Handler(void) { hal::dma_channel_isr(17); }  /* DMA0_CH6 */
extern "C" void IRQ56_Handler(void) { hal::dma_channel_isr(56); }  /* DMA1_CH0 */
extern "C" void IRQ57_Handler(void) { hal::dma_channel_isr(57); }  /* DMA1_CH1 */
