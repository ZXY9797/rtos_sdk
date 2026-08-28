#include <drivers/pwm.h>

namespace hal {

// STM32H7 general-purpose timer register layout.
struct TimRegs {
    volatile uint32_t CR1;    // 0x00
    volatile uint32_t CR2;    // 0x04
    volatile uint32_t SMCR;   // 0x08
    volatile uint32_t DIER;   // 0x0C
    volatile uint32_t SR;     // 0x10
    volatile uint32_t EGR;    // 0x14
    volatile uint32_t CCMR1;  // 0x18
    volatile uint32_t CCMR2;  // 0x1C
    volatile uint32_t CCER;   // 0x20
    volatile uint32_t CNT;    // 0x24
    volatile uint32_t PSC;    // 0x28
    volatile uint32_t ARR;    // 0x2C
    volatile uint32_t RCR;    // 0x30
    volatile uint32_t CCR1;   // 0x34
    volatile uint32_t CCR2;   // 0x38
    volatile uint32_t CCR3;   // 0x3C
    volatile uint32_t CCR4;   // 0x40
    volatile uint32_t BDTR;   // 0x44
    volatile uint32_t DCR;    // 0x48
    volatile uint32_t DMAR;   // 0x4C
};

// CR1
constexpr uint32_t CR1_CEN   = (1U << 0);
constexpr uint32_t CR1_ARPE  = (1U << 7);
constexpr uint32_t CR1_CMS_Pos = 5;
// CCMR1 output compare modes.
constexpr uint32_t CCMR1_OC1M_Pos = 4;
constexpr uint32_t CCMR1_OC1M_PWM1 = (6U << 4);
constexpr uint32_t CCMR1_OC1PE     = (1U << 3);
constexpr uint32_t CCMR1_OC2M_Pos = 12;
constexpr uint32_t CCMR1_OC2M_PWM1 = (6U << 12);
constexpr uint32_t CCMR1_OC2PE     = (1U << 11);
// CCMR2 OC3/OC4
constexpr uint32_t CCMR2_OC3M_Pos = 4;
constexpr uint32_t CCMR2_OC3M_PWM1 = (6U << 4);
constexpr uint32_t CCMR2_OC3PE     = (1U << 3);
constexpr uint32_t CCMR2_OC4M_Pos = 12;
constexpr uint32_t CCMR2_OC4M_PWM1 = (6U << 12);
constexpr uint32_t CCMR2_OC4PE     = (1U << 11);
// CCER
constexpr uint32_t CCER_CC1E = (1U << 0);
constexpr uint32_t CCER_CC2E = (1U << 4);
constexpr uint32_t CCER_CC3E = (1U << 8);
constexpr uint32_t CCER_CC4E = (1U << 12);
// DIER
constexpr uint32_t DIER_UIE = (1U << 0);
// EGR
constexpr uint32_t EGR_UG   = (1U << 0);

Status PwmBase::init(const PwmConfig &config) {
    if (!is_valid_pwm_channel(m_channel)
        || config.period == 0U
        || config.three_phase || config.center_aligned
        || config.complementary || config.dead_time != 0U) {
        return Status::NotSupported;
    }
    auto *regs = reinterpret_cast<TimRegs *>(m_base);

    regs->CR1 = 0;
    regs->PSC = config.prescaler;
    regs->ARR = config.period;
    regs->CR1 = CR1_ARPE;

    // Configure only the requested channel.
    switch (m_channel) {
    case PwmChannel::Ch1:
        regs->CCMR1 =
            (regs->CCMR1 & ~(0x7U << CCMR1_OC1M_Pos))
            | CCMR1_OC1M_PWM1 | CCMR1_OC1PE;
        break;
    case PwmChannel::Ch2:
        regs->CCMR1 =
            (regs->CCMR1 & ~(0x7U << CCMR1_OC2M_Pos))
            | CCMR1_OC2M_PWM1 | CCMR1_OC2PE;
        break;
    case PwmChannel::Ch3:
        regs->CCMR2 =
            (regs->CCMR2 & ~(0x7U << CCMR2_OC3M_Pos))
            | CCMR2_OC3M_PWM1 | CCMR2_OC3PE;
        break;
    case PwmChannel::Ch4:
        regs->CCMR2 =
            (regs->CCMR2 & ~(0x7U << CCMR2_OC4M_Pos))
            | CCMR2_OC4M_PWM1 | CCMR2_OC4PE;
        break;
    default:
        return Status::InvalidArgument;
    }

    // MOE is not touched here because general-purpose timers have no BDTR.

    m_initialized = true;
    return Status::Ok;
}

Status PwmBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);
    regs->CR1 = 0;
    m_initialized = false;
    return Status::Ok;
}

Status PwmBase::start() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);

    switch (m_channel) {
    case PwmChannel::Ch1: regs->CCER |= CCER_CC1E; break;
    case PwmChannel::Ch2: regs->CCER |= CCER_CC2E; break;
    case PwmChannel::Ch3: regs->CCER |= CCER_CC3E; break;
    case PwmChannel::Ch4: regs->CCER |= CCER_CC4E; break;
    default: return Status::InvalidArgument;
    }
    regs->CR1 |= CR1_CEN;
    return Status::Ok;
}

Status PwmBase::stop() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);

    regs->CR1 &= ~CR1_CEN;
    switch (m_channel) {
    case PwmChannel::Ch1: regs->CCER &= ~CCER_CC1E; break;
    case PwmChannel::Ch2: regs->CCER &= ~CCER_CC2E; break;
    case PwmChannel::Ch3: regs->CCER &= ~CCER_CC3E; break;
    case PwmChannel::Ch4: regs->CCER &= ~CCER_CC4E; break;
    default: return Status::InvalidArgument;
    }
    return Status::Ok;
}

Status PwmBase::set_pulse(uint32_t pulse) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);
    if (pulse > regs->ARR) return Status::InvalidArgument;

    switch (m_channel) {
    case PwmChannel::Ch1: regs->CCR1 = pulse; break;
    case PwmChannel::Ch2: regs->CCR2 = pulse; break;
    case PwmChannel::Ch3: regs->CCR3 = pulse; break;
    case PwmChannel::Ch4: regs->CCR4 = pulse; break;
    default: return Status::InvalidArgument;
    }
    return Status::Ok;
}

Status PwmBase::set_pulse(PwmChannel ch, uint32_t pulse) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);
    if (pulse > regs->ARR) return Status::InvalidArgument;

    switch (ch) {
    case PwmChannel::Ch1: regs->CCR1 = pulse; break;
    case PwmChannel::Ch2: regs->CCR2 = pulse; break;
    case PwmChannel::Ch3: regs->CCR3 = pulse; break;
    case PwmChannel::Ch4: regs->CCR4 = pulse; break;
    default: return Status::InvalidArgument;
    }
    return Status::Ok;
}

Status PwmBase::set_three_phase_pulses(uint32_t channel_1,
                                       uint32_t channel_2,
                                       uint32_t channel_3)
{
    (void)channel_1;
    (void)channel_2;
    (void)channel_3;
    return Status::NotSupported;
}

Status PwmBase::enable_output() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);
    regs->CCER |= CCER_CC1E
        << (static_cast<uint8_t>(m_channel) * 4U);
    return Status::Ok;
}

Status PwmBase::disable_output() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);
    regs->CCER &= ~(CCER_CC1E
        << (static_cast<uint8_t>(m_channel) * 4U));
    return Status::Ok;
}

Status PwmBase::set_update_callback(IrqCallback cb, void *arg) {
    m_update_cb = cb;
    m_update_arg = arg;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);
    if (cb) {
        regs->DIER |= DIER_UIE;
    } else {
        regs->DIER &= ~DIER_UIE;
    }
    return Status::Ok;
}

} // namespace hal
