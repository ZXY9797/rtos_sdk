#include <drivers/pwm.h>
#include <gd32_regs.h>

namespace hal {

using namespace gd32;

namespace {

// TIMER register bits.
// CTL0
constexpr uint32_t CTL0_CEN    = (1U << 0);
constexpr uint32_t CTL0_ARPE   = (1U << 7);
constexpr uint32_t CTL0_CAM_Msk = (3U << 5);
constexpr uint32_t CTL0_CAM_2  = (3U << 5);
constexpr uint32_t CTL0_UPDIS  = (1U << 1);
// CTL1 master-mode trigger output.
constexpr uint32_t CTL1_MMC_Msk = (7U << 4);
constexpr uint32_t CTL1_MMC_TRGO = (2U << 4);
// CHCTL0 output compare modes for channels 0 and 1.
constexpr uint32_t CHCTL0_OC0M_Pos = 4;
constexpr uint32_t CHCTL0_OC0M_Msk = (7U << 4);
constexpr uint32_t CHCTL0_OC0M_PWM1 = (6U << 4);
constexpr uint32_t CHCTL0_OC0PE = (1U << 3);
constexpr uint32_t CHCTL0_OC1M_Pos = 12;
constexpr uint32_t CHCTL0_OC1M_Msk = (7U << 12);
constexpr uint32_t CHCTL0_OC1M_PWM1 = (6U << 12);
constexpr uint32_t CHCTL0_OC1PE = (1U << 11);
// CHCTL1 output compare mode for channel 2.
constexpr uint32_t CHCTL1_OC2M_Pos = 4;
constexpr uint32_t CHCTL1_OC2M_Msk = (7U << 4);
constexpr uint32_t CHCTL1_OC2M_PWM1 = (6U << 4);
constexpr uint32_t CHCTL1_OC2PE = (1U << 3);
// CHCTL1 output compare mode for channel 3.
constexpr uint32_t CHCTL1_OC3M_Pos = 12;
constexpr uint32_t CHCTL1_OC3M_Msk = (7U << 12);
constexpr uint32_t CHCTL1_OC3M_PWM1 = (6U << 12);
constexpr uint32_t CHCTL1_OC3PE = (1U << 11);
// CHCTL2 output enables and polarities.
constexpr uint32_t CHCTL2_CH0EN  = (1U << 0);
constexpr uint32_t CHCTL2_CH0NEN = (1U << 2);
constexpr uint32_t CHCTL2_CH1EN  = (1U << 4);
constexpr uint32_t CHCTL2_CH1NEN = (1U << 6);
constexpr uint32_t CHCTL2_CH2EN  = (1U << 8);
constexpr uint32_t CHCTL2_CH2NEN = (1U << 10);
constexpr uint32_t CHCTL2_CH3EN  = (1U << 12);
// CCHP dead time and main output enable.
constexpr uint32_t CCHP_MOE   = (1U << 15);
// SWEVG
constexpr uint32_t SWEVG_UPG  = (1U << 0);
// DMAINTEN
constexpr uint32_t DMAINTEN_UPIE = (1U << 0);
// INTF
constexpr uint32_t INTF_UPIF = (1U << 0);
// Only advanced timers provide CCHP and complementary outputs.
constexpr uint32_t kApb2Timer0Enable = (1U << 11);
constexpr uint32_t kApb2Timer7Enable = (1U << 13);
constexpr uint32_t kApb1Timer1Enable = (1U << 0);
constexpr uintptr_t kTimerRegisterBlockStride = 0x400U;
constexpr uint32_t kDeadTimeMask = 0xFFU;

static constexpr bool is_advanced_timer(uintptr_t base) {
    return base == TIMER0_BASE || base == TIMER7_BASE;
}

constexpr uint32_t kMainThreePhaseMask =
    CHCTL2_CH0EN | CHCTL2_CH1EN | CHCTL2_CH2EN;
constexpr uint32_t kComplementaryThreePhaseMask =
    CHCTL2_CH0NEN | CHCTL2_CH1NEN | CHCTL2_CH2NEN;

[[nodiscard]] constexpr uint32_t channel_mask(PwmChannel channel)
{
    return CHCTL2_CH0EN << (static_cast<uint8_t>(channel) * 4U);
}

void configure_channel(TimerRegs &regs, PwmChannel channel)
{
    switch (channel) {
    case PwmChannel::Ch1:
        regs.CHCTL0 = (regs.CHCTL0 & ~CHCTL0_OC0M_Msk)
                    | CHCTL0_OC0M_PWM1 | CHCTL0_OC0PE;
        break;
    case PwmChannel::Ch2:
        regs.CHCTL0 = (regs.CHCTL0 & ~CHCTL0_OC1M_Msk)
                    | CHCTL0_OC1M_PWM1 | CHCTL0_OC1PE;
        break;
    case PwmChannel::Ch3:
        regs.CHCTL1 = (regs.CHCTL1 & ~CHCTL1_OC2M_Msk)
                    | CHCTL1_OC2M_PWM1 | CHCTL1_OC2PE;
        break;
    case PwmChannel::Ch4:
        regs.CHCTL1 = (regs.CHCTL1 & ~CHCTL1_OC3M_Msk)
                    | CHCTL1_OC3M_PWM1 | CHCTL1_OC3PE;
        break;
    default:
        break;
    }
}

void configure_three_phase(TimerRegs &regs)
{
    configure_channel(regs, PwmChannel::Ch1);
    configure_channel(regs, PwmChannel::Ch2);
    configure_channel(regs, PwmChannel::Ch3);
}

[[nodiscard]] bool enable_timer_clock(uintptr_t base)
{
    switch (base) {
    case TIMER0_BASE:
        rcu_apb2en() |= kApb2Timer0Enable;
        break;
    case TIMER1_BASE:
    case TIMER2_BASE:
    case TIMER3_BASE:
    case TIMER4_BASE:
    case TIMER5_BASE:
    case TIMER6_BASE: {
        const uint32_t index = static_cast<uint32_t>(
            (base - TIMER1_BASE) / kTimerRegisterBlockStride);
        rcu_apb1en() |= kApb1Timer1Enable << index;
        break;
    }
    case TIMER7_BASE:
        rcu_apb2en() |= kApb2Timer7Enable;
        break;
    default:
        return false;
    }
    return true;
}

} // namespace

Status PwmBase::init(const PwmConfig &config) {
    if (!is_valid_pwm_channel(m_channel)
        || config.period == 0U
        || config.dead_time > kDeadTimeMask
        || (config.dead_time != 0U && !config.complementary)
        || (config.complementary && !is_advanced_timer(m_base))) {
        return Status::InvalidArgument;
    }
    if (!enable_timer_clock(m_base)) {
        return Status::NotSupported;
    }
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    if (m_initialized && (regs->CTL0 & CTL0_CEN) != 0U) {
        return Status::Busy;
    }
    regs->CTL0 = 0;
    regs->PSC = config.prescaler;
    regs->CAR = config.period;
    regs->CTL0 = CTL0_ARPE;
    if (config.center_aligned) {
        regs->CTL0 |= CTL0_CAM_2;
    }
    m_three_phase = config.three_phase || config.complementary;
    m_complementary = config.complementary;
    if (m_three_phase) {
        configure_three_phase(*regs);
    } else {
        configure_channel(*regs, m_channel);
    }
    regs->CHCTL2 &= ~(kMainThreePhaseMask
                    | kComplementaryThreePhaseMask
                    | CHCTL2_CH3EN);
    if (is_advanced_timer(m_base)) {
        regs->CCHP &= ~CCHP_MOE;
        regs->CCHP = (regs->CCHP & ~kDeadTimeMask)
                   | (config.dead_time & kDeadTimeMask);
    }
    if (m_three_phase) {
        regs->CTL1 = (regs->CTL1 & ~CTL1_MMC_Msk) | CTL1_MMC_TRGO;
        regs->CREP = 0;
    }
    m_initialized = true;
    return Status::Ok;
}

Status PwmBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    (void)disable_output();
    regs->DMAINTEN &= ~DMAINTEN_UPIE;
    regs->CTL0 = 0;
    m_update_cb = nullptr;
    m_update_arg = nullptr;
    m_initialized = false;
    m_three_phase = false;
    m_complementary = false;
    return Status::Ok;
}

Status PwmBase::start() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    regs->CTL0 |= CTL0_CEN;
    return Status::Ok;
}

Status PwmBase::stop() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    regs->CTL0 &= ~CTL0_CEN;
    return Status::Ok;
}

Status PwmBase::set_pulse(uint32_t pulse) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    if (pulse > regs->CAR) return Status::InvalidArgument;

    switch (m_channel) {
    case PwmChannel::Ch1: regs->CH0CV = pulse; break;
    case PwmChannel::Ch2: regs->CH1CV = pulse; break;
    case PwmChannel::Ch3: regs->CH2CV = pulse; break;
    case PwmChannel::Ch4: regs->CH3CV = pulse; break;
    default: return Status::InvalidArgument;
    }
    return Status::Ok;
}

Status PwmBase::set_pulse(PwmChannel ch, uint32_t pulse) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    if (pulse > regs->CAR) return Status::InvalidArgument;

    switch (ch) {
    case PwmChannel::Ch1: regs->CH0CV = pulse; break;
    case PwmChannel::Ch2: regs->CH1CV = pulse; break;
    case PwmChannel::Ch3: regs->CH2CV = pulse; break;
    case PwmChannel::Ch4: regs->CH3CV = pulse; break;
    default: return Status::InvalidArgument;
    }
    return Status::Ok;
}

Status PwmBase::set_three_phase_pulses(uint32_t channel_1,
                                       uint32_t channel_2,
                                       uint32_t channel_3)
{
    if (!m_initialized || !m_three_phase) {
        return Status::InvalidArgument;
    }
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    if (channel_1 > regs->CAR || channel_2 > regs->CAR
        || channel_3 > regs->CAR) {
        return Status::InvalidArgument;
    }
    const uint32_t update_disable = regs->CTL0 & CTL0_UPDIS;
    regs->CTL0 |= CTL0_UPDIS;
    regs->CH0CV = channel_1;
    regs->CH1CV = channel_2;
    regs->CH2CV = channel_3;
    regs->CTL0 = (regs->CTL0 & ~CTL0_UPDIS) | update_disable;
    return Status::Ok;
}

Status PwmBase::enable_output() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    if (m_three_phase) {
        // Transfer the complete neutral/duty shadow set before exposing pins.
        regs->SWEVG = SWEVG_UPG;
        regs->CHCTL2 |= kMainThreePhaseMask;
        if (m_complementary) {
            regs->CHCTL2 |= kComplementaryThreePhaseMask;
        }
    } else {
        regs->CHCTL2 |= channel_mask(m_channel);
    }
    if (is_advanced_timer(m_base)) {
        regs->CCHP |= CCHP_MOE;
    }
    return Status::Ok;
}

Status PwmBase::disable_output() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    if (is_advanced_timer(m_base)) {
        regs->CCHP &= ~CCHP_MOE;
    }
    if (m_three_phase) {
        regs->CHCTL2 &= ~(kMainThreePhaseMask
                        | kComplementaryThreePhaseMask);
    } else {
        regs->CHCTL2 &= ~channel_mask(m_channel);
    }
    return Status::Ok;
}

Status PwmBase::set_update_callback(IrqCallback cb, void *arg) {
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    regs->DMAINTEN &= ~DMAINTEN_UPIE;
    m_update_cb = cb;
    m_update_arg = arg;
    regs->INTF = INTF_UPIF;
    if (cb) {
        regs->DMAINTEN |= DMAINTEN_UPIE;
    }
    return Status::Ok;
}

void PwmBase::isr_handler(osal::IsrContext& context) {
    (void)context;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    if ((regs->INTF & INTF_UPIF) == 0U) {
        return;
    }

    regs->INTF = INTF_UPIF;
    if (m_update_cb != nullptr) {
        m_update_cb(m_update_arg);
    }
}

} // namespace hal
