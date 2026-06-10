#include <drivers/dma.h>
#include <assert.h>
#include <utility>

#include "gd32_regs.h"
#include "gd32f50x_dma.h"

namespace hal {

/// DMA 控制器索引
static int controller_index(uint32_t dma_base) {
    if (dma_base == DMA0_BASE) return 0;
    if (dma_base == DMA1_BASE) return 1;
    return -1;
}

/// DMA 通道占用状态（每个 DMA 控制器 7 个通道）
static uint8_t s_dma_channel_occupied[2] = {0, 0};

int8_t DmaChannelPool::allocate(uint32_t dma_base) {
    int idx = controller_index(dma_base);
    if (idx < 0) return -1;
    uint8_t &occupied = s_dma_channel_occupied[idx];
    for (uint8_t i = 0; i < 7; i++) {
        if (!(occupied & (1U << i))) {
            occupied |= (1U << i);
            return static_cast<int8_t>(i);
        }
    }
    return -1;
}

void DmaChannelPool::release(uint32_t dma_base, int8_t channel) {
    int idx = controller_index(dma_base);
    if (idx >= 0 && channel >= 0 && channel < 7) {
        s_dma_channel_occupied[idx] &= ~(1U << channel);
    }
}

DmaChannel DmaChannel::request(uint32_t dma_base, uint8_t mux_channel) {
    int8_t ch = DmaChannelPool::allocate(dma_base);
    if (ch < 0) {
        return DmaChannel(dma_base, 0xFF, mux_channel);  // 无效通道
    }
    return DmaChannel(dma_base, static_cast<uint8_t>(ch), mux_channel);
}

DmaChannel::~DmaChannel() {
    if (m_ch != 0xFF) {
        (void)stop();
        DmaChannelPool::release(m_dma, m_ch);
    }
}

DmaChannel::DmaChannel(DmaChannel &&other) noexcept
    : m_dma(other.m_dma), m_ch(other.m_ch), m_mux_ch(other.m_mux_ch) {
    other.m_ch = 0xFF;  // 标记源对象为无效
}

DmaChannel &DmaChannel::operator=(DmaChannel &&other) noexcept {
    if (this != &other) {
        // 释放当前通道
        if (m_ch != 0xFF) {
            (void)stop();
            DmaChannelPool::release(m_dma, m_ch);
        }
        // 转移所有权
        m_dma = other.m_dma;
        m_ch = other.m_ch;
        m_mux_ch = other.m_mux_ch;
        other.m_ch = 0xFF;
    }
    return *this;
}

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
    if (m_ch > 6) return Status::InvalidArgument;
    reinterpret_cast<gd32::DmaRegs *>(m_dma)->CH[m_ch].CTL |= DMA_CHXCTL_CHEN;
    return Status::Ok;
}

Status DmaChannel::stop() {
    if (m_ch > 6) return Status::InvalidArgument;
    reinterpret_cast<gd32::DmaRegs *>(m_dma)->CH[m_ch].CTL &= ~DMA_CHXCTL_CHEN;
    return Status::Ok;
}

bool DmaChannel::is_transfer_complete() const {
    if (m_ch > 6) return false;
    auto *dma = reinterpret_cast<const gd32::DmaRegs *>(m_dma);
    return (dma->INTF & (DMA_INTF_FTFIF << (m_ch * 4U))) != 0;
}

void DmaChannel::clear_flags() {
    if (m_ch > 6) return;
    auto *dma = reinterpret_cast<gd32::DmaRegs *>(m_dma);
    dma->INTC = (DMA_INTC_GIFC | DMA_INTC_FTFIFC | DMA_INTC_HTFIFC | DMA_INTC_ERRIFC)
                << (m_ch * 4U);
}

} // namespace hal
