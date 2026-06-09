#include <drivers/pwm.h>

namespace hal {

// TIM 寄存器结构体（映射 STM32H7 通用定时器）
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
// CCMR1 OC1M 位（输出比较模式）
constexpr uint32_t CCMR1_OC1M_Pos = 4;
constexpr uint32_t CCMR1_OC1M_PWM1 = (6U << 4); // PWM 模式 1
constexpr uint32_t CCMR1_OC1PE     = (1U << 3);  // 预装载使能
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
// BDTR
constexpr uint32_t BDTR_MOE = (1U << 15);
// DIER
constexpr uint32_t DIER_UIE = (1U << 0); // 更新中断使能
// EGR
constexpr uint32_t EGR_UG   = (1U << 0);

Status PwmBase::init(const PwmConfig &config) {
    auto *regs = reinterpret_cast<TimRegs *>(m_base);

    regs->CR1 = 0;
    regs->PSC = config.prescaler;
    regs->ARR = config.period;
    regs->CR1 = CR1_ARPE;

    // 配置对应通道为 PWM 模式
    switch (m_channel) {
    case PwmChannel::Ch1:
        regs->CCMR1 = (regs->CCMR1 & ~(0x7U << CCMR1_OC1M_Pos)) | CCMR1_OC1M_PWM1 | CCMR1_OC1PE;
        break;
    case PwmChannel::Ch2:
        regs->CCMR1 = (regs->CCMR1 & ~(0x7U << CCMR1_OC2M_Pos)) | CCMR1_OC2M_PWM1 | CCMR1_OC2PE;
        break;
    case PwmChannel::Ch3:
        regs->CCMR2 = (regs->CCMR2 & ~(0x7U << CCMR2_OC3M_Pos)) | CCMR2_OC3M_PWM1 | CCMR2_OC3PE;
        break;
    case PwmChannel::Ch4:
        regs->CCMR2 = (regs->CCMR2 & ~(0x7U << CCMR2_OC4M_Pos)) | CCMR2_OC4M_PWM1 | CCMR2_OC4PE;
        break;
    }

    // 使能 BDTR MOE（仅高级定时器 TIM1/TIM8/TIM20 有 BDTR 寄存器）
    // 通用定时器 (TIM2-TIM5, TIM12-TIM15) 没有 BDTR，访问偏移 0x44 会 HardFault
    // 注意: STM32 高级定时器基地址需与实际芯片匹配
    // 当前由 enable_output() 单独控制 MOE，此处不再无条件写入

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
    }
    return Status::Ok;
}

Status PwmBase::set_pulse(uint32_t pulse) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);

    switch (m_channel) {
    case PwmChannel::Ch1: regs->CCR1 = pulse; break;
    case PwmChannel::Ch2: regs->CCR2 = pulse; break;
    case PwmChannel::Ch3: regs->CCR3 = pulse; break;
    case PwmChannel::Ch4: regs->CCR4 = pulse; break;
    }
    return Status::Ok;
}

Status PwmBase::set_pulse(PwmChannel ch, uint32_t pulse) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);

    switch (ch) {
    case PwmChannel::Ch1: regs->CCR1 = pulse; break;
    case PwmChannel::Ch2: regs->CCR2 = pulse; break;
    case PwmChannel::Ch3: regs->CCR3 = pulse; break;
    case PwmChannel::Ch4: regs->CCR4 = pulse; break;
    }
    return Status::Ok;
}

Status PwmBase::enable_output() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);
    regs->BDTR |= BDTR_MOE;
    return Status::Ok;
}

Status PwmBase::disable_output() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimRegs *>(m_base);
    regs->BDTR &= ~BDTR_MOE;
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
