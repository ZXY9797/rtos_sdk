#include <drivers/dma.h>
#include <arch/arch_interface.h>

#include "gd32_regs.h"
#include "gd32f50x_dma.h"

namespace hal {

static volatile uint32_t &mux_cfg(uint8_t mux_ch) {
    return *reinterpret_cast<volatile uint32_t *>(DMAMUX_BASE + mux_ch * 4U);
}

Status DmaChannel::config(const DmaConfig &cfg) {
    if (m_ch > 6) return Status::InvalidArgument;

    auto *dma = reinterpret_cast<gd32::DmaRegs *>(m_dma);
    auto &ch = dma->CH[m_ch];

    ch.CTL &= ~DMA_CHXCTL_CHEN;
    clear_flags();

    mux_cfg(m_mux_ch) = cfg.request_id;

    uint32_t ctl = 0;
    if (cfg.direction == DmaDirection::MemoryToPeriph) ctl |= DMA_CHXCTL_DIR;
    if (cfg.circular)     ctl |= DMA_CHXCTL_CMEN;
    if (cfg.periph_inc)   ctl |= DMA_CHXCTL_PNAGA;
    if (cfg.memory_inc)   ctl |= DMA_CHXCTL_MNAGA;
    ctl |= (static_cast<uint32_t>(cfg.periph_width) << 8U) & DMA_CHXCTL_PWIDTH;
    ctl |= (static_cast<uint32_t>(cfg.memory_width) << 10U) & DMA_CHXCTL_MWIDTH;
    ctl |= (static_cast<uint32_t>(cfg.priority) << 12U) & DMA_CHXCTL_PRIO;
    if (cfg.direction == DmaDirection::MemoryToMemory) ctl |= DMA_CHXCTL_M2M;
    ctl |= DMA_CHXCTL_FTFIE;

    ch.CTL = ctl;
    ch.CNT = cfg.count & 0xFFFFU;
    ch.PADDR = cfg.periph_addr;
    ch.MADDR = cfg.memory_addr;

    return Status::Ok;
}

Status DmaChannel::start() {
    reinterpret_cast<gd32::DmaRegs *>(m_dma)->CH[m_ch].CTL |= DMA_CHXCTL_CHEN;
    return Status::Ok;
}

Status DmaChannel::stop() {
    reinterpret_cast<gd32::DmaRegs *>(m_dma)->CH[m_ch].CTL &= ~DMA_CHXCTL_CHEN;
    return Status::Ok;
}

bool DmaChannel::is_transfer_complete() const {
    auto *dma = reinterpret_cast<const gd32::DmaRegs *>(m_dma);
    return (dma->INTF & (DMA_INTF_FTFIF << (m_ch * 4U))) != 0;
}

void DmaChannel::clear_flags() {
    auto *dma = reinterpret_cast<gd32::DmaRegs *>(m_dma);
    dma->INTC = (DMA_INTC_GIFC | DMA_INTC_FTFIFC | DMA_INTC_HTFIFC | DMA_INTC_ERRIFC)
                << (m_ch * 4U);
}

} // namespace hal
