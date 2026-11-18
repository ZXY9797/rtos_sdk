#include <drivers/adc.h>
#include <gd32_regs.h>

#include <cstddef>

namespace hal {
namespace {

using gd32::AdcRegs;

constexpr uint32_t kAdcEnable = (1U << 0);
constexpr uint32_t kSoftwareInsertedStart = (1U << 21);
constexpr uint32_t kSoftwareRoutineStart = (1U << 22);
constexpr uint32_t kInsertedTriggerModeMask = (3U << 13);
constexpr uint32_t kInsertedInterruptEnable = (1U << 7);
constexpr uint32_t kWatchdogChannelMask = 0x1FU;
constexpr uint32_t kWatchdogSingleChannel = (1U << 9);
constexpr uint32_t kRoutineWatchdogEnable = (1U << 23);
constexpr uint32_t kRoutineComplete = (1U << 1);
constexpr uint32_t kInsertedComplete = (1U << 2);
constexpr uint32_t kSequenceLengthShift = 20U;
constexpr uint32_t kSequenceLengthMask = (3U << kSequenceLengthShift);
constexpr uint32_t kChannelMask = 0x1FU;
constexpr uint32_t kSampleTimeMask = 0x7U;
constexpr uint32_t kResolutionShift = 12U;
constexpr uint32_t kResolutionMask = (3U << kResolutionShift);
constexpr uint32_t kAdcDataMask = 0x0FFFU;
constexpr uint32_t kConversionTimeoutCycles = 100000U;
constexpr uint32_t kAdcPrescalerMask = (3U << 14) | (1U << 28);
constexpr uint32_t kAdcPrescalerDivideByFour = (1U << 14);
constexpr uint32_t kAdcAhbPrescalerSelect = (1U << 29);

static_assert(offsetof(AdcRegs, CTL0) == 0x04U);
static_assert(offsetof(AdcRegs, RSQ2) == 0x34U);
static_assert(offsetof(AdcRegs, LDATA) == 0x3CU);
static_assert(offsetof(AdcRegs, OVSAMPCTL) == 0x80U);

[[nodiscard]] bool valid_channel(AdcChannel channel, uintptr_t base)
{
    return static_cast<uint8_t>(channel) <= (base == ADC2_BASE ? 16U : 17U);
}

[[nodiscard]] bool valid_sample_time(AdcSampleTime sample_time)
{
    return static_cast<uint8_t>(sample_time)
        <= static_cast<uint8_t>(AdcSampleTime::Cycles480);
}

[[nodiscard]] bool resolution_bits(uint32_t resolution, uint32_t &bits)
{
    switch (resolution) {
    case 12U: bits = 0U; break;
    case 10U: bits = 1U; break;
    case 8U: bits = 2U; break;
    case 6U: bits = 3U; break;
    default: return false;
    }
    return true;
}

void configure_adc_clock(uintptr_t base)
{
    gd32::rcu_cfg0() =
        (gd32::rcu_cfg0() & ~kAdcPrescalerMask)
        | kAdcPrescalerDivideByFour;
    gd32::rcu_cfg1() &= ~kAdcAhbPrescalerSelect;
    const uint32_t enable = base == ADC0_BASE ? (1U << 9U) :
        base == ADC1_BASE ? (1U << 10U) : (1U << 15U);
    gd32::rcu_apb2en() |= enable;
}

void write_sample_time(AdcRegs &regs, AdcChannel adc_channel,
                       AdcSampleTime sample_time)
{
    const uint32_t channel = static_cast<uint32_t>(adc_channel);
    const uint32_t sample = static_cast<uint32_t>(sample_time);
    if (channel < 10U) {
        const uint32_t shift = channel * 3U;
        regs.SAMPT1 = (regs.SAMPT1 & ~(kSampleTimeMask << shift))
            | (sample << shift);
    } else {
        const uint32_t shift = (channel - 10U) * 3U;
        regs.SAMPT0 = (regs.SAMPT0 & ~(kSampleTimeMask << shift))
            | (sample << shift);
    }
}

void configure_latch_source(AdcRegs &regs, uint32_t latch,
                            uint32_t inserted_rank)
{
    const uint32_t shift = 24U - latch * 8U;
    constexpr uint32_t kLatchFieldMask = 0x8FU;
    regs.LDCTL = (regs.LDCTL & ~(kLatchFieldMask << shift))
        | (inserted_rank << shift);
}

} // namespace

void AdcBase::isr_handler(osal::IsrContext &context)
{
    (void)context;
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    if ((regs.STAT & kInsertedComplete) == 0U) {
        return;
    }
    regs.STAT = ~kInsertedComplete;
    if (m_eoc_cb != nullptr) {
        m_eoc_cb(m_eoc_arg);
    }
}

Status AdcBase::init(const AdcConfig &config)
{
    uint32_t resolution = 0U;
    const bool adc_valid = m_base == ADC0_BASE || m_base == ADC1_BASE || m_base == ADC2_BASE;
    const uint16_t ratio = config.oversample_ratio;
    if (m_initialized || !adc_valid || !resolution_bits(config.resolution, resolution) ||
        ratio == 0U || ratio > 256U || (ratio & (ratio - 1U)) != 0U ||
        config.oversample_shift > 8U) {
        return Status::InvalidArgument;
    }
    const uint64_t full_scale = (((uint64_t(1U) << config.resolution) - 1U) * ratio) >>
                                (ratio > 1U ? config.oversample_shift : 0U);
    if (full_scale > UINT16_MAX) return Status::InvalidArgument;
    configure_adc_clock(m_base);
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    regs.CTL0 = 0U;
    regs.CTL1 = 0U;
    regs.RSQ0 = 0U;
    regs.ISQ = 0U;
    regs.OVSAMPCTL =
        (regs.OVSAMPCTL & ~kResolutionMask)
        | (resolution << kResolutionShift);
    if (ratio > 1U) {
        uint32_t ratio_code = 0U;
        for (uint16_t remaining = ratio; remaining > 2U; remaining >>= 1U) ++ratio_code;
        regs.OVSAMPCTL = (regs.OVSAMPCTL & ~0x1FFU) | 1U | (ratio_code << 2U) |
                            (uint32_t(config.oversample_shift) << 5U);
    } else regs.OVSAMPCTL &= ~1U;
    m_scan_dma = config.scan_dma;
    regs.CTL1 |= kAdcEnable;
    m_injected_count = 0U;
    m_initialized = true;
    return Status::Ok;
}

Status AdcBase::deinit()
{
    if (!m_initialized) {
        return Status::Ok;
    }
    (void)stop_scan();
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    regs.CTL0 &= ~kInsertedInterruptEnable;
    regs.CTL1 &= ~kAdcEnable;
    m_eoc_cb = nullptr;
    m_eoc_arg = nullptr;
    m_injected_count = 0U;
    m_initialized = false;
    return Status::Ok;
}

Status AdcBase::read(AdcChannel channel, uint16_t &value)
{
    if (!m_initialized || m_scan_buffer != nullptr || !valid_channel(channel, m_base)) {
        return Status::InvalidArgument;
    }
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    regs.CTL1 |= kAdcEnable;
    regs.RSQ0 &= ~(0xFU << kSequenceLengthShift);
    regs.RSQ2 = static_cast<uint32_t>(channel) & kChannelMask;
    regs.STAT = ~kRoutineComplete;
    regs.CTL1 |= kSoftwareRoutineStart;
    uint32_t remaining = kConversionTimeoutCycles;
    while ((regs.STAT & kRoutineComplete) == 0U && remaining > 0U) {
        --remaining;
    }
    if ((regs.STAT & kRoutineComplete) == 0U) {
        return Status::Timeout;
    }
    value = static_cast<uint16_t>(regs.RDATA);
    return Status::Ok;
}

Status AdcBase::set_sample_time(AdcChannel channel,
                                AdcSampleTime sample_time)
{
    if (!m_initialized || !valid_channel(channel, m_base)
        || !valid_sample_time(sample_time)) {
        return Status::InvalidArgument;
    }
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    write_sample_time(regs, channel, sample_time);
    return Status::Ok;
}

Status AdcBase::config_injected(const AdcInjectedConfig &config)
{
    if (!m_initialized || m_scan_buffer != nullptr || config.channel_count == 0U
        || config.channel_count > 4U || config.trigger_source != 0U) {
        return Status::InvalidArgument;
    }
    for (uint32_t rank = 0U; rank < config.channel_count; ++rank) {
        if (!valid_channel(config.channels[rank].channel, m_base)
            || !valid_sample_time(config.channels[rank].sample_time)) {
            return Status::InvalidArgument;
        }
    }
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    const uint32_t length = config.channel_count - 1U;
    regs.ISQ = (regs.ISQ & ~kSequenceLengthMask)
        | (length << kSequenceLengthShift);
    for (uint32_t rank = 0U; rank < config.channel_count; ++rank) {
        const uint32_t shift = 15U - (length - rank) * 5U;
        regs.ISQ = (regs.ISQ & ~(kChannelMask << shift))
            | (static_cast<uint32_t>(config.channels[rank].channel)
               << shift);
        write_sample_time(regs, config.channels[rank].channel,
                          config.channels[rank].sample_time);
        configure_latch_source(regs, rank, rank);
    }
    regs.CTL1 &= ~kInsertedTriggerModeMask;
    m_injected_count = static_cast<uint8_t>(config.channel_count);
    return Status::Ok;
}

Status AdcBase::start_injected()
{
    if (!m_initialized || m_injected_count == 0U) {
        return Status::InvalidArgument;
    }
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    regs.STAT = ~kInsertedComplete;
    regs.CTL1 |= kSoftwareInsertedStart;
    return Status::Ok;
}

Status AdcBase::read_injected(uint8_t rank, uint16_t &value)
{
    if (!m_initialized || rank >= m_injected_count) {
        return Status::InvalidArgument;
    }
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    value = static_cast<uint16_t>(regs.LDATA[rank] & kAdcDataMask);
    return Status::Ok;
}

Status AdcBase::config_watchdog(AdcChannel channel,
                                uint16_t high_threshold,
                                uint16_t low_threshold)
{
    if (!m_initialized || !valid_channel(channel, m_base)
        || high_threshold > kAdcDataMask
        || low_threshold > high_threshold) {
        return Status::InvalidArgument;
    }
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    regs.WDHIGH = high_threshold;
    regs.WDLOW = low_threshold;
    regs.CTL0 = (regs.CTL0 & ~kWatchdogChannelMask)
        | static_cast<uint32_t>(channel)
        | kWatchdogSingleChannel | kRoutineWatchdogEnable;
    return Status::Ok;
}

Status AdcBase::set_eoc_callback(IrqCallback callback, void *argument)
{
    if (!m_initialized || m_base != ADC0_BASE) {
        return Status::NotSupported;
    }
    auto &regs = *reinterpret_cast<AdcRegs *>(m_base);
    regs.CTL0 &= ~kInsertedInterruptEnable;
    m_eoc_cb = callback;
    m_eoc_arg = argument;
    if (callback != nullptr) {
        regs.STAT = ~kInsertedComplete;
        regs.CTL0 |= kInsertedInterruptEnable;
    }
    return Status::Ok;
}

} // namespace hal
