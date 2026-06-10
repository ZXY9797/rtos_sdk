#include <drivers/pwm.h>
#include <gd32_regs.h>

namespace hal {

using namespace gd32;

// TIMER 寄存器位定义
// CTL0
constexpr uint32_t CTL0_CEN    = (1U << 0);
constexpr uint32_t CTL0_ARPE   = (1U << 7);
constexpr uint32_t CTL0_CAM_Pos = 5;  // 中心对齐模式
constexpr uint32_t CTL0_CAM_Msk = (3U << 5);
constexpr uint32_t CTL0_CAM_0  = (1U << 5); // CAM=01: 中心对齐模式1
constexpr uint32_t CTL0_CAM_1  = (2U << 5); // CAM=10: 中心对齐模式2
constexpr uint32_t CTL0_CAM_2  = (3U << 5); // CAM=11: 中心对齐模式3
constexpr uint32_t CTL0_UPDIS  = (1U << 1); // 更新禁用
// CTL1
constexpr uint32_t CTL1_TI0S   = (1U << 7); // TI0 选择
// CHCTL0 — CH0/CH1 输出比较模式
constexpr uint32_t CHCTL0_OC0M_Pos = 4;
constexpr uint32_t CHCTL0_OC0M_Msk = (7U << 4);
constexpr uint32_t CHCTL0_OC0M_PWM1 = (6U << 4);
constexpr uint32_t CHCTL0_OC0PE = (1U << 3);
constexpr uint32_t CHCTL0_OC1M_Pos = 12;
constexpr uint32_t CHCTL0_OC1M_Msk = (7U << 12);
constexpr uint32_t CHCTL0_OC1M_PWM1 = (6U << 12);
constexpr uint32_t CHCTL0_OC1PE = (1U << 11);
// CHCTL1 — CH2/CH3 输出比较模式
constexpr uint32_t CHCTL1_OC2M_Pos = 4;
constexpr uint32_t CHCTL1_OC2M_Msk = (7U << 4);
constexpr uint32_t CHCTL1_OC2M_PWM1 = (6U << 4);
constexpr uint32_t CHCTL1_OC2PE = (1U << 3);
// CHCTL1 — CH3 输出比较模式
constexpr uint32_t CHCTL1_OC3M_Pos = 12;
constexpr uint32_t CHCTL1_OC3M_Msk = (7U << 12);
constexpr uint32_t CHCTL1_OC3M_PWM1 = (6U << 12);
constexpr uint32_t CHCTL1_OC3PE = (1U << 11);
// CHCTL2 — 输出使能/极性
constexpr uint32_t CHCTL2_CH0EN  = (1U << 0);
constexpr uint32_t CHCTL2_CH0P   = (1U << 1);
constexpr uint32_t CHCTL2_CH0NEN = (1U << 2);
constexpr uint32_t CHCTL2_CH0NP  = (1U << 3);
constexpr uint32_t CHCTL2_CH1EN  = (1U << 4);
constexpr uint32_t CHCTL2_CH1P   = (1U << 5);
constexpr uint32_t CHCTL2_CH1NEN = (1U << 6);
constexpr uint32_t CHCTL2_CH1NP  = (1U << 7);
constexpr uint32_t CHCTL2_CH2EN  = (1U << 8);
constexpr uint32_t CHCTL2_CH2P   = (1U << 9);
constexpr uint32_t CHCTL2_CH2NEN = (1U << 10);
constexpr uint32_t CHCTL2_CH2NP  = (1U << 11);
constexpr uint32_t CHCTL2_CH3EN  = (1U << 12);
// CCHP — 死区和 MOE
constexpr uint32_t CCHP_MOE   = (1U << 15);
constexpr uint32_t CCHP_ROS   = (1U << 12); // 运行模式下输出安全状态
constexpr uint32_t CCHP_IOS   = (1U << 11); // 空闲模式下输出安全状态
constexpr uint32_t CCHP_OIS0  = (1U << 8);
constexpr uint32_t CCHP_OIS1  = (1U << 9);
constexpr uint32_t CCHP_OIS2  = (1U << 10);
// SWEVG
constexpr uint32_t SWEVG_UPG  = (1U << 0); // 软件更新事件
// DMAINTEN
constexpr uint32_t DMAINTEN_UPIE = (1U << 0); // 更新中断使能
// INTF
constexpr uint32_t INTF_UPIF = (1U << 0); // 更新中断标志
// SMCFG
constexpr uint32_t SMCFG_MMC_Pos = 4; // 主模式选择
constexpr uint32_t SMCFG_MMC_Msk = (7U << 4);
constexpr uint32_t SMCFG_MMC_TRGO = (2U << 4); // 更新事件 → TRGO

// 更新中断号 — 使用 SoC 头文件定义
#include <irq.h>
static constexpr int TIMER0_UP_IRQn = 25; // TIMER0_UP_TIMER9_IRQn (from CMSIS)
static constexpr int TIMER5_IRQn    = 54; // TIMER5_IRQn
static constexpr int TIMER6_IRQn    = 55; // TIMER6_IRQn

// TIMER0 ISR 回调存储
static PwmBase::IrqCallback s_timer0_update_cb = nullptr;
static void *s_timer0_update_arg = nullptr;

extern "C" void TIMER0_UP_TIMER9_IRQHandler() {
    auto *regs = reinterpret_cast<TimerRegs *>(TIMER0_BASE);
    if (regs->INTF & INTF_UPIF) {
        regs->INTF = INTF_UPIF; // 写 1 清除 (GD32 rc_w1)
        if (s_timer0_update_cb) {
            s_timer0_update_cb(s_timer0_update_arg);
        }
    }
}

// TIMER5 ISR 回调存储
static PwmBase::IrqCallback s_timer5_update_cb = nullptr;
static void *s_timer5_update_arg = nullptr;

extern "C" void TIMER5_IRQHandler() {
    auto *regs = reinterpret_cast<TimerRegs *>(TIMER5_BASE);
    if (regs->INTF & INTF_UPIF) {
        regs->INTF = INTF_UPIF;
        if (s_timer5_update_cb) {
            s_timer5_update_cb(s_timer5_update_arg);
        }
    }
}

// TIMER6 ISR 回调存储
static PwmBase::IrqCallback s_timer6_update_cb = nullptr;
static void *s_timer6_update_arg = nullptr;

extern "C" void TIMER6_IRQHandler() {
    auto *regs = reinterpret_cast<TimerRegs *>(TIMER6_BASE);
    if (regs->INTF & INTF_UPIF) {
        regs->INTF = INTF_UPIF;
        if (s_timer6_update_cb) {
            s_timer6_update_cb(s_timer6_update_arg);
        }
    }
}

// 检测是否为高级定时器(TIMER0/TIMER7) — 有 CCHP 寄存器
static constexpr bool is_advanced_timer(uintptr_t base) {
    return base == TIMER0_BASE || base == TIMER7_BASE;
}

Status PwmBase::init(const PwmConfig &config) {
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);

    // 停止定时器
    regs->CTL0 = 0;
    regs->PSC = config.prescaler;
    regs->CAR = config.period;

    if (config.complementary && is_advanced_timer(m_base)) {
        // === 三相互补 PWM 模式 ===

        // 中心对齐模式 3（上下计数，CCR 在两个方向都比较）
        regs->CTL0 = (regs->CTL0 & ~CTL0_CAM_Msk) | CTL0_CAM_2 | CTL0_ARPE;

        // 死区时间
        regs->CCHP = (regs->CCHP & 0xFF00) | (config.dead_time & 0xFF);

        // CH0/CH1/CH2: PWM 模式 1 + 预装载
        regs->CHCTL0 = (regs->CHCTL0 & ~(CHCTL0_OC0M_Msk | CHCTL0_OC1M_Msk))
                       | CHCTL0_OC0M_PWM1 | CHCTL0_OC0PE
                       | CHCTL0_OC1M_PWM1 | CHCTL0_OC1PE;
        regs->CHCTL1 = (regs->CHCTL1 & ~CHCTL1_OC2M_Msk)
                       | CHCTL1_OC2M_PWM1 | CHCTL1_OC2PE;

        // 使能 CH0/CH1/CH2 正向 + 互补输出 (先清后设)
        regs->CHCTL2 = (regs->CHCTL2
                       & ~(CHCTL2_CH0EN | CHCTL2_CH0NEN
                         | CHCTL2_CH1EN | CHCTL2_CH1NEN
                         | CHCTL2_CH2EN | CHCTL2_CH2NEN))
                       | CHCTL2_CH0EN | CHCTL2_CH0NEN
                       | CHCTL2_CH1EN | CHCTL2_CH1NEN
                       | CHCTL2_CH2EN | CHCTL2_CH2NEN;

        // 配置 TRGO — 更新事件作为 ADC 触发源
        regs->CTL1 = (regs->CTL1 & ~SMCFG_MMC_Msk) | SMCFG_MMC_TRGO;

        // REP 寄存器 = 0（每次更新事件都产生 TRGO）
        regs->CREP = 0;
    } else {
        // === 简单单通道 PWM 模式 ===
        regs->CTL0 = CTL0_ARPE;

        // 配置对应通道为 PWM 模式 1
        switch (m_channel) {
        case PwmChannel::Ch1:
            regs->CHCTL0 = (regs->CHCTL0 & ~CHCTL0_OC0M_Msk)
                           | CHCTL0_OC0M_PWM1 | CHCTL0_OC0PE;
            break;
        case PwmChannel::Ch2:
            regs->CHCTL0 = (regs->CHCTL0 & ~CHCTL0_OC1M_Msk)
                           | CHCTL0_OC1M_PWM1 | CHCTL0_OC1PE;
            break;
        case PwmChannel::Ch3:
            regs->CHCTL1 = (regs->CHCTL1 & ~CHCTL1_OC2M_Msk)
                           | CHCTL1_OC2M_PWM1 | CHCTL1_OC2PE;
            break;
        case PwmChannel::Ch4:
            regs->CHCTL1 = (regs->CHCTL1 & ~CHCTL1_OC3M_Msk)
                           | CHCTL1_OC3M_PWM1 | CHCTL1_OC3PE;
            break;
        }

        // 高级定时器需要 MOE
        if (is_advanced_timer(m_base)) {
            regs->CCHP |= CCHP_MOE;
        }
    }

    m_initialized = true;
    return Status::Ok;
}

Status PwmBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    regs->CTL0 = 0;
    m_initialized = false;
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

    switch (m_channel) {
    case PwmChannel::Ch1: regs->CH0CV = pulse; break;
    case PwmChannel::Ch2: regs->CH1CV = pulse; break;
    case PwmChannel::Ch3: regs->CH2CV = pulse; break;
    case PwmChannel::Ch4: regs->CH3CV = pulse; break;
    }
    return Status::Ok;
}

Status PwmBase::set_pulse(PwmChannel ch, uint32_t pulse) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);

    switch (ch) {
    case PwmChannel::Ch1: regs->CH0CV = pulse; break;
    case PwmChannel::Ch2: regs->CH1CV = pulse; break;
    case PwmChannel::Ch3: regs->CH2CV = pulse; break;
    case PwmChannel::Ch4: regs->CH3CV = pulse; break;
    }
    return Status::Ok;
}

Status PwmBase::enable_output() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    regs->CCHP |= CCHP_MOE;
    return Status::Ok;
}

Status PwmBase::disable_output() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    regs->CCHP &= ~CCHP_MOE;
    return Status::Ok;
}

Status PwmBase::set_update_callback(IrqCallback cb, void *arg) {
    m_update_cb = cb;
    m_update_arg = arg;

    PwmBase::IrqCallback *dst_cb = nullptr;
    void **dst_arg = nullptr;
    int irqn = -1;

    if (m_base == TIMER0_BASE) {
        dst_cb = &s_timer0_update_cb;
        dst_arg = &s_timer0_update_arg;
        irqn = TIMER0_UP_IRQn;
    } else if (m_base == TIMER5_BASE) {
        dst_cb = &s_timer5_update_cb;
        dst_arg = &s_timer5_update_arg;
        irqn = TIMER5_IRQn;
    } else if (m_base == TIMER6_BASE) {
        dst_cb = &s_timer6_update_cb;
        dst_arg = &s_timer6_update_arg;
        irqn = TIMER6_IRQn;
    } else {
        return Status::NotSupported;
    }

    *dst_cb = cb;
    *dst_arg = arg;

    auto *regs = reinterpret_cast<TimerRegs *>(m_base);
    regs->INTF = INTF_UPIF;
    if (cb) {
        regs->DMAINTEN |= DMAINTEN_UPIE;
        hal::Irq::enable(irqn);
    } else {
        regs->DMAINTEN &= ~DMAINTEN_UPIE;
    }
    return Status::Ok;
}

} // namespace hal
