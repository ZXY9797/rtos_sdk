#include <drivers/dma.h>
#include <atomic>
#include <utility>

#include "gd32_regs.h"
#include "gd32f50x_dma.h"

namespace hal {

static int controller_index(uint32_t dma_base) {
    if (dma_base == DMA0_BASE) return 0;
    if (dma_base == DMA1_BASE) return 1;
    return -1;
}

/* Allocation can run concurrently during independent device initialization. */
static std::atomic<uint8_t> s_dma_channel_occupied[2] {};
static_assert(std::atomic<uint8_t>::is_always_lock_free);

static uint8_t channel_count(uint32_t dma_base) {
    if (dma_base == DMA0_BASE) return 7U;
    if (dma_base == DMA1_BASE) return 5U;
    return 0U;
}

[[nodiscard]] static bool valid_mux_channel(uint32_t dma_base,
                                            uint8_t mux_channel)
{
    if (dma_base == DMA0_BASE) {
        return mux_channel < 7U;
    }
    if (dma_base == DMA1_BASE) {
        return mux_channel >= 7U && mux_channel < 12U;
    }
    return false;
}

[[nodiscard]] static bool valid_config(const DmaConfig &config)
{
    return config.count > 0U
        && config.periph_addr != 0U
        && config.memory_addr != 0U
        && config.request_id <= 0xFFU
        && static_cast<uint8_t>(config.direction)
            <= static_cast<uint8_t>(DmaDirection::MemoryToMemory)
        && static_cast<uint8_t>(config.periph_width)
            <= static_cast<uint8_t>(DmaWidth::Word)
        && static_cast<uint8_t>(config.memory_width)
            <= static_cast<uint8_t>(DmaWidth::Word)
        && static_cast<uint8_t>(config.priority)
            <= static_cast<uint8_t>(DmaPriority::VeryHigh);
}

int8_t DmaChannelPool::allocate(uint32_t dma_base) {
    const int idx = controller_index(dma_base);
    if (idx < 0) return -1;

    std::atomic<uint8_t> &occupied = s_dma_channel_occupied[idx];
    for (uint8_t channel = 0U;
         channel < channel_count(dma_base); ++channel) {
        const uint8_t mask = static_cast<uint8_t>(1U << channel);
        const uint8_t previous = occupied.fetch_or(
            mask, std::memory_order_acq_rel);
        if ((previous & mask) == 0U) {
            return static_cast<int8_t>(channel);
        }
    }
    return -1;
}

void DmaChannelPool::release(uint32_t dma_base, int8_t channel) {
    const int idx = controller_index(dma_base);
    if (idx >= 0 && channel >= 0 &&
        static_cast<uint8_t>(channel) < channel_count(dma_base)) {
        const uint8_t mask = static_cast<uint8_t>(1U << channel);
        s_dma_channel_occupied[idx].fetch_and(
            static_cast<uint8_t>(~mask), std::memory_order_release);
    }
}

DmaChannel DmaChannel::request(uint32_t dma_base, uint8_t mux_channel) {
    if (!valid_mux_channel(dma_base, mux_channel)) {
        return DmaChannel(dma_base, 0xFF, mux_channel);
    }
    int8_t ch = DmaChannelPool::allocate(dma_base);
    if (ch < 0) {
        return DmaChannel(dma_base, 0xFF, mux_channel);
    }
    DmaChannel result(dma_base, static_cast<uint8_t>(ch), mux_channel);
    result.m_owned = true;
    return result;
}

DmaChannel::~DmaChannel() {
    if (m_ch != 0xFF) {
        (void)stop();
        if (m_owned) {
            DmaChannelPool::release(m_dma, m_ch);
        }
    }
}

DmaChannel::DmaChannel(DmaChannel &&other) noexcept
    : m_dma(other.m_dma), m_ch(other.m_ch), m_mux_ch(other.m_mux_ch),
      m_owned(other.m_owned) {
    other.m_owned = false;
    other.m_ch = 0xFF;
}

DmaChannel &DmaChannel::operator=(DmaChannel &&other) noexcept {
    if (this != &other) {
        if (m_ch != 0xFF) {
            (void)stop();
            if (m_owned) {
                DmaChannelPool::release(m_dma, m_ch);
            }
        }
        m_dma = other.m_dma;
        m_ch = other.m_ch;
        m_mux_ch = other.m_mux_ch;
        m_owned = other.m_owned;
        other.m_ch = 0xFF;
        other.m_owned = false;
    }
    return *this;
}

static volatile uint32_t &mux_cfg(uint8_t mux_ch) {
    return *reinterpret_cast<volatile uint32_t *>(DMAMUX_BASE + mux_ch * 4U);
}

Status DmaChannel::config(const DmaConfig &cfg) {
    if (m_ch >= channel_count(m_dma)
        || !valid_mux_channel(m_dma, m_mux_ch)
        || !valid_config(cfg)) {
        return Status::InvalidArgument;
    }

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
    if (m_ch >= channel_count(m_dma)) return Status::InvalidArgument;
    reinterpret_cast<gd32::DmaRegs *>(m_dma)->CH[m_ch].CTL |= DMA_CHXCTL_CHEN;
    return Status::Ok;
}

Status DmaChannel::stop() {
    if (m_ch >= channel_count(m_dma)) return Status::InvalidArgument;
    reinterpret_cast<gd32::DmaRegs *>(m_dma)->CH[m_ch].CTL &= ~DMA_CHXCTL_CHEN;
    return Status::Ok;
}

bool DmaChannel::is_transfer_complete() const {
    if (m_ch >= channel_count(m_dma)) return false;
    auto *dma = reinterpret_cast<const gd32::DmaRegs *>(m_dma);
    return (dma->INTF & (DMA_INTF_FTFIF << (m_ch * 4U))) != 0;
}

void DmaChannel::clear_flags() {
    if (m_ch >= channel_count(m_dma)) return;
    auto *dma = reinterpret_cast<gd32::DmaRegs *>(m_dma);
    dma->INTC = (DMA_INTC_GIFC | DMA_INTC_FTFIFC
                 | DMA_INTC_HTFIFC | DMA_INTC_ERRIFC)
                << (m_ch * 4U);
}

} // namespace hal
