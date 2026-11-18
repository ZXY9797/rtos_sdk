#include <drivers/adc.h>
#include <gd32_regs.h>

namespace hal {
namespace {
constexpr uint32_t kRoutineTriggerMask = 3U << 18U;
constexpr uint32_t kRoutineDmaEnable = 1U << 8U;
constexpr uint32_t kContinuous = 1U << 1U;
constexpr uintptr_t kTriggerMuxBase = 0x40014400U;
}

Status AdcBase::start_scan(const AdcScanConfig& config, uint16_t* buffer,
                           size_t count, ScanCallback callback, void* argument)
{
    if (!m_initialized || m_scan_buffer || m_injected_count || !m_scan_dma.is_valid() || !buffer ||
        !callback || config.channel_count == 0U || config.channel_count > 16U ||
        count != size_t(config.channel_count) * 2U ||
        (reinterpret_cast<uintptr_t>(buffer) & 1U) != 0U ||
        (config.trigger_input != AdcScanConfig::software_trigger && config.trigger_input > 0x7FU))
        return Status::InvalidArgument;
    for (uint8_t rank = 0U; rank < config.channel_count; ++rank) {
        if (static_cast<unsigned>(config.channels[rank].channel) > (m_base == ADC2_BASE ? 16U : 17U) ||
            static_cast<unsigned>(config.channels[rank].sample_time) > 7U)
            return Status::InvalidArgument;
    }
    const uint32_t base = static_cast<uint32_t>(m_base);
    gd32::rcu_ahben() |= (m_scan_dma.controller == DMA0_BASE ? 1U : 2U) | (1U << 19U);
    auto& adc = *reinterpret_cast<gd32::AdcRegs*>(m_base);
    adc.CTL1 |= kRoutineTriggerMask;
    adc.CTL0 |= 1U << 8U;
    adc.RSQ0 = uint32_t(config.channel_count-1U) << 20U;
    adc.RSQ1 = adc.RSQ2 = 0U;
    for (uint8_t rank = 0U; rank < config.channel_count; ++rank) {
        volatile uint32_t& sequence = rank < 6U ? adc.RSQ2 : rank < 12U ? adc.RSQ1 : adc.RSQ0;
        sequence |= uint32_t(config.channels[rank].channel) << ((rank % 6U) * 5U);
        const auto status = set_sample_time(config.channels[rank].channel, config.channels[rank].sample_time);
        if (status != Status::Ok) return status;
    }
    DmaConfig dma {};
    dma.request_id = m_scan_dma.request_id;
    dma.periph_addr = base + offsetof(gd32::AdcRegs, RDATA);
    dma.memory_addr = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(buffer));
    dma.count = static_cast<uint16_t>(count);
    dma.direction = DmaDirection::PeriphToMemory;
    dma.periph_width = dma.memory_width = DmaWidth::HalfWord;
    dma.priority = DmaPriority::VeryHigh;
    dma.memory_inc = dma.circular = true;
    m_scan_channel = DmaChannel(static_cast<uint32_t>(m_scan_dma.controller),
                                m_scan_dma.channel, m_scan_dma.mux_channel);
    if (m_scan_channel.config(dma) != Status::Ok) return Status::HardwareError;
    auto& channel = reinterpret_cast<gd32::DmaRegs*>(m_scan_dma.controller)->CH[m_scan_dma.channel];
    channel.CTL |= (1U << 2U) | (1U << 3U); // Half complete and transfer error.
    m_scan_buffer = buffer;
    m_scan_count = config.channel_count;
    m_scan_callback = callback;
    m_scan_argument = argument;
    adc.CTL1 |= kRoutineDmaEnable | 1U;
    if (m_scan_channel.start() != Status::Ok) { (void)stop_scan(); return Status::HardwareError; }
    if (config.trigger_input == AdcScanConfig::software_trigger) {
        adc.CTL1 |= kContinuous | (1U << 22U);
    } else {
        gd32::rcu_apb2en() |= 1U << 24U;
        const uint32_t offset = m_base == ADC0_BASE ? 0x38U : m_base == ADC1_BASE ? 0x40U : 0x48U;
        auto& mux = *reinterpret_cast<volatile uint32_t*>(kTriggerMuxBase + offset);
        if ((mux & (1U << 31U)) && (mux & 0xFFU) != config.trigger_input) {
            (void)stop_scan(); return Status::HardwareError;
        }
        mux = (mux & ~0xFFU) | config.trigger_input;
        adc.CTL1 &= ~kRoutineTriggerMask;
    }
    return Status::Ok;
}

Status AdcBase::stop_scan()
{
    if (!m_scan_buffer) return Status::Ok;
    auto& adc = *reinterpret_cast<gd32::AdcRegs*>(m_base);
    adc.CTL1 = (adc.CTL1 | kRoutineTriggerMask) & ~(kContinuous | kRoutineDmaEnable | 1U);
    const auto result = m_scan_channel.stop();
    m_scan_channel.clear_flags();
    m_scan_buffer = nullptr;
    m_scan_callback = nullptr;
    m_scan_count = 0U;
    return result;
}

void AdcBase::scan_dma_isr(osal::IsrContext& context)
{
    (void)context;
    if (!m_scan_buffer) return;
    auto& dma = *reinterpret_cast<gd32::DmaRegs*>(m_scan_dma.controller);
    const uint32_t flags = (dma.INTF >> (m_scan_dma.channel * 4U)) & 0xFU;
    dma.INTC = 0xFU << (m_scan_dma.channel * 4U);
    const bool half = (flags & (1U << 2U)) != 0U;
    const bool full = (flags & (1U << 1U)) != 0U;
    if ((flags & (1U << 3U)) || (half && full)) {
        const auto callback = m_scan_callback;
        void* argument = m_scan_argument;
        (void)stop_scan();
        callback(argument, nullptr, 0U, Status::HardwareError);
    } else if (half || full) {
        m_scan_callback(m_scan_argument, m_scan_buffer + (full ? m_scan_count : 0U),
                        m_scan_count, Status::Ok);
    }
}

} // namespace hal
