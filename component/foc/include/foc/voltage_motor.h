#pragma once

#include <foc/voltage_foc.h>

#include <device.h>
#include <drivers/pwm.h>

#include <atomic>
#include <cstdint>

namespace foc {

struct VoltageMotorConfig {
    VoltageFocConfig foc {};
    uint32_t pwm_period_counts {3000U};
    hal::PwmChannel phase_u {hal::PwmChannel::Ch1};
    hal::PwmChannel phase_v {hal::PwmChannel::Ch2};
    hal::PwmChannel phase_w {hal::PwmChannel::Ch3};
};

class VoltageMotor {
public:
    explicit VoltageMotor(hal::PwmBase &pwm) : pwm_(pwm) {}
    ~VoltageMotor();

    VoltageMotor(const VoltageMotor &) = delete;
    VoltageMotor &operator=(const VoltageMotor &) = delete;

    [[nodiscard]] hal::Status init(const VoltageMotorConfig &config);
    [[nodiscard]] hal::Status deinit();
    [[nodiscard]] hal::Status arm();
    void disarm();
    [[nodiscard]] hal::Status apply(float electrical_angle_rad,
                                    float d_axis_voltage_v,
                                    float q_axis_voltage_v,
                                    float bus_voltage_v);
    [[nodiscard]] bool is_initialized() const { return is_initialized_; }
    [[nodiscard]] bool is_armed() const
    {
        return is_armed_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] hal::Status write_duty(const PhaseDuty &duty);

    hal::PwmBase &pwm_;
    VoltageMotorConfig config_ {};
    VoltageFoc foc_ {};
    std::atomic<bool> is_armed_ {false};
    bool is_initialized_ {false};
};

template <int PwmOrdinal, uint8_t ChannelU, uint8_t ChannelV,
          uint8_t ChannelW>
class VoltageMotorDevice {
    static_assert(ChannelU <= 2U);
    static_assert(ChannelV <= 2U);
    static_assert(ChannelW <= 2U);
    static_assert(ChannelU != ChannelV);
    static_assert(ChannelU != ChannelW);
    static_assert(ChannelV != ChannelW);

public:
    VoltageMotorDevice()
        : motor_(hal::device_get<PwmOrdinal>())
    {
    }

    [[nodiscard]] int init(const VoltageMotorConfig &config)
    {
        return static_cast<int>(motor_.init(config));
    }

    [[nodiscard]] int deinit()
    {
        return static_cast<int>(motor_.deinit());
    }

    [[nodiscard]] bool is_initialized() const
    {
        return motor_.is_initialized();
    }

    [[nodiscard]] VoltageMotor &motor() { return motor_; }

private:
    VoltageMotor motor_;
};

} // namespace foc
