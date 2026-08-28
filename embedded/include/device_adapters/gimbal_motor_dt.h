#pragma once

#include <device.h>
#include <gimbal/voltage_motor.h>

#include <cstdint>

#define HAL_GIMBAL_MOTOR_DT_ADAPT(node_id)                         \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kChannelU =                      \
            DT_PROP(node_id, pwm_ch_u);                            \
        static constexpr uint32_t kChannelV =                      \
            DT_PROP(node_id, pwm_ch_v);                            \
        static constexpr uint32_t kChannelW =                      \
            DT_PROP(node_id, pwm_ch_w);                            \
        static constexpr uint32_t kPeriod =                        \
            DT_PROP(node_id, pwm_period_counts);                   \
        static constexpr uint32_t kNominalBusMv =                  \
            DT_PROP(node_id, nominal_bus_mv);                      \
        static constexpr uint32_t kMaximumModulationMilli =        \
            DT_PROP(node_id, maximum_modulation_milli);            \
                                                                   \
        static_assert(kChannelU <= 2U && kChannelV <= 2U           \
                      && kChannelW <= 2U,                          \
                      "Motor PWM must use channels 1 through 3");  \
        static_assert(kChannelU != kChannelV                       \
                      && kChannelU != kChannelW                    \
                      && kChannelV != kChannelW,                   \
                      "Motor PWM channels must be unique");       \
        static_assert(kPeriod > 0U && kNominalBusMv > 0U,          \
                      "Motor PWM period and voltage must be set");\
        static_assert(kPeriod == DeviceTrait<                     \
                          DT_ORD(DT_PHANDLE(node_id, pwm_dev))     \
                      >::kPeriod,                                 \
                      "Motor and PWM timer periods must match"); \
        static_assert(kMaximumModulationMilli > 0U                 \
                      && kMaximumModulationMilli < 1000U,          \
                      "Motor modulation is outside (0, 1)");        \
                                                                   \
        using type = gimbal::VoltageMotorDevice<                   \
            DT_ORD(DT_PHANDLE(node_id, pwm_dev)),                  \
            static_cast<uint8_t>(kChannelU),                       \
            static_cast<uint8_t>(kChannelV),                       \
            static_cast<uint8_t>(kChannelW)>;                      \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            gimbal::VoltageMotorConfig config {};                  \
            config.pwm_period_counts = kPeriod;                    \
            config.phase_u = static_cast<hal::PwmChannel>(         \
                kChannelU);                                        \
            config.phase_v = static_cast<hal::PwmChannel>(         \
                kChannelV);                                        \
            config.phase_w = static_cast<hal::PwmChannel>(         \
                kChannelW);                                        \
            config.foc.nominal_bus_voltage_v =                     \
                static_cast<float>(kNominalBusMv) * 0.001F;        \
            config.foc.maximum_modulation =                        \
                static_cast<float>(kMaximumModulationMilli)        \
                    * 0.001F;                                      \
            return instance.init(config);                          \
        }                                                          \
    };
