#pragma once

#include <device.h>
#include <foc/motor_device.h>
#include <cstdint>

#define HAL_FOC_MOTOR_DT_ADAPT(node_id)                            \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr float kMilli = 0.001F;                    \
        static constexpr float kMicro = 0.000001F;                 \
        static constexpr uint32_t kPolePairs =                     \
            DT_PROP_OR(node_id, pole_pairs, 7U);                   \
        static constexpr uint32_t kChannelU =                      \
            DT_PROP_OR(node_id, pwm_ch_u, 0U);                     \
        static constexpr uint32_t kChannelV =                      \
            DT_PROP_OR(node_id, pwm_ch_v, 1U);                     \
        static constexpr uint32_t kChannelW =                      \
            DT_PROP_OR(node_id, pwm_ch_w, 2U);                     \
        static constexpr uint32_t kSensorMode =                    \
            DT_PROP_OR(node_id, sensor_mode, 0U);                  \
        static constexpr uint32_t kControlMode =                   \
            DT_PROP_OR(node_id, control_mode, 1U);                 \
                                                                   \
        static_assert(kPolePairs > 0U && kPolePairs <= UINT8_MAX,  \
                      "Motor pole-pair count does not fit uint8_t");\
        static_assert(kChannelU <= 3U && kChannelV <= 3U &&        \
                      kChannelW <= 3U,                             \
                      "Motor PWM channel is invalid");             \
        static_assert(kChannelU != kChannelV &&                    \
                      kChannelU != kChannelW &&                    \
                      kChannelV != kChannelW,                      \
                      "Motor PWM channels must be unique");        \
        static_assert(kSensorMode <= 3U,                           \
                      "Motor sensor mode is invalid");             \
        static_assert(kControlMode <= 3U,                          \
                      "Motor control mode is invalid");            \
                                                                   \
        using type = foc::MotorDevice<                             \
            DT_ORD(DT_PHANDLE(node_id, pwm_dev)),                  \
            DT_ORD(DT_PHANDLE(node_id, adc_dev)),                  \
            kChannelU, kChannelV, kChannelW>;                      \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            foc::MotorConfig config{};                             \
            config.rs = static_cast<float>(DT_PROP_OR(             \
                node_id, rs_milliohm, 50U)) * kMilli;              \
            config.ld = static_cast<float>(DT_PROP_OR(             \
                node_id, ld_microhenry, 100U)) * kMicro;           \
            config.lq = static_cast<float>(DT_PROP_OR(             \
                node_id, lq_microhenry, 100U)) * kMicro;           \
            config.flux_linkage = static_cast<float>(DT_PROP_OR(   \
                node_id, flux_milliweber, 0U)) * kMilli;           \
            config.pole_pairs = static_cast<uint8_t>(kPolePairs);  \
            config.imax = static_cast<float>(DT_PROP_OR(           \
                node_id, max_current_ma, 20000U)) * kMilli;        \
            config.vmax = static_cast<float>(DT_PROP_OR(           \
                node_id, max_voltage_mv, 48000U)) * kMilli;        \
            config.foc.pwm_frequency = DT_PROP_OR(                 \
                node_id, pwm_frequency, 20000U);                   \
            config.foc.current_bandwidth = DT_PROP_OR(             \
                node_id, current_bandwidth, 1000U);                \
            config.sensor = static_cast<foc::SensorMode>(          \
                kSensorMode);                                     \
            config.control = static_cast<foc::ControlMode>(        \
                kControlMode);                                    \
            return instance.init(config);                          \
        }                                                          \
    };
