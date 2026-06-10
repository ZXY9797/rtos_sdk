#pragma once

#include <device.h>
#include <foc/motor.h>
#include <new>
#include <cstdint>

namespace foc {

/// MotorDevice — 设备树驱动包装器
///
/// 模板参数:
///   PwmOrd  - PWM 设备的 DeviceTrait ordinal
///   AdcOrd  - ADC 设备的 DeviceTrait ordinal
///   ChU/V/W - 三相 PWM 通道索引 (0=Ch1, 1=Ch2, 2=Ch3)
template <int PwmOrd, int AdcOrd, int ChU, int ChV, int ChW>
class MotorDevice {
public:
    MotorDevice() = default;

    /// init() 由 gen_device_traits.py 生成的 initcall 调用
    /// 需要 drivers_generated.h 中 device_get 的定义可见
    int init(const MotorConfig &cfg);

    Motor &motor() { return *motor_; }
    const Motor &motor() const { return *motor_; }

private:
    alignas(Motor) uint8_t storage_[sizeof(Motor)];
    Motor *motor_ = nullptr;

    static void pwm_isr_callback(void *arg) {
        static_cast<Motor *>(arg)->fast_loop_isr();
    }
};

template <int PwmOrd, int AdcOrd, int ChU, int ChV, int ChW>
int MotorDevice<PwmOrd, AdcOrd, ChU, ChV, ChW>::init(const MotorConfig &cfg) {
    auto &pwm = hal::device_get<PwmOrd>();
    auto &adc = hal::device_get<AdcOrd>();

    motor_ = new (&storage_) Motor(
        cfg, pwm,
        static_cast<hal::PwmChannel>(ChU),
        static_cast<hal::PwmChannel>(ChV),
        static_cast<hal::PwmChannel>(ChW),
        adc);

    motor_->init();

    (void)pwm.set_update_callback(pwm_isr_callback, motor_);

    return 0;
}

} // namespace foc
