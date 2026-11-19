#pragma once

#include <device.h>
#include <foc/motor_device.h>

#include <cstdint>

#define HAL_FOC_MOTOR_DT_ADAPT(node_id)                                  \
    template <>                                                          \
    struct DeviceTrait<DT_ORD(node_id)> {                                \
        static constexpr float kMilli = 0.001F;                          \
        static constexpr float kMicro = 0.000001F;                       \
        static constexpr uint32_t kPolePairs =                           \
            DT_PROP(node_id, pole_pairs);                                \
        static constexpr uint32_t kChannelU =                            \
            DT_PROP(node_id, pwm_ch_u);                                  \
        static constexpr uint32_t kChannelV =                            \
            DT_PROP(node_id, pwm_ch_v);                                  \
        static constexpr uint32_t kChannelW =                            \
            DT_PROP(node_id, pwm_ch_w);                                  \
        static constexpr uint32_t kSensorMode =                          \
            DT_PROP(node_id, sensor_mode);                               \
        static constexpr uint32_t kControlMode =                         \
            DT_PROP(node_id, control_mode);                              \
        static constexpr uint32_t kPwmFrequency =                        \
            DT_PROP(node_id, pwm_frequency);                             \
        static constexpr uint32_t kCurrentBandwidth =                    \
            DT_PROP(node_id, current_bandwidth);                         \
        static constexpr uint32_t kTimerClockHz =                        \
            DT_PROP(node_id, timer_clock_hz);                            \
        static constexpr uint32_t kPwmPrescaler =                        \
            DT_PROP(node_id, pwm_prescaler);                             \
        static constexpr uint32_t kDeadTimeNs =                          \
            DT_PROP(node_id, dead_time_ns);                              \
        static constexpr uint32_t kAdcResolution =                       \
            DT_PROP(node_id, adc_resolution_bits);                       \
        static constexpr uint32_t kCurrentUChannel =                     \
            DT_PROP(node_id, current_u_channel);                         \
        static constexpr uint32_t kCurrentWChannel =                     \
            DT_PROP(node_id, current_w_channel);                         \
        static constexpr uint32_t kVbusChannel =                         \
            DT_PROP(node_id, vbus_channel);                              \
        static constexpr uint32_t kAdcReferenceMv =                      \
            DT_PROP(node_id, adc_reference_mv);                          \
        static constexpr uint32_t kCurrentZeroCode =                     \
            DT_PROP(node_id, current_zero_code);                         \
                                                                          \
        static_assert(kPolePairs > 0U && kPolePairs <= UINT8_MAX,        \
                      "Motor pole-pair count does not fit uint8_t");     \
        static_assert(kChannelU <= 3U && kChannelV <= 3U                 \
                      && kChannelW <= 3U,                                \
                      "Motor PWM channel is invalid");                  \
        static_assert(kChannelU != kChannelV                             \
                      && kChannelU != kChannelW                          \
                      && kChannelV != kChannelW,                         \
                      "Motor PWM channels must be unique");             \
        static_assert(kSensorMode <= 3U,                                 \
                      "Motor sensor mode is invalid");                  \
        static_assert(kControlMode <= 3U,                                \
                      "Motor control mode is invalid");                 \
        static_assert(DT_PROP(node_id, rs_milliohm) > 0U                 \
                      && DT_PROP(node_id, ld_microhenry) > 0U            \
                      && DT_PROP(node_id, lq_microhenry) > 0U,           \
                      "Motor electrical parameters must be positive");  \
        static_assert(DT_PROP(node_id, max_current_ma) > 0U              \
                      && DT_PROP(node_id, max_voltage_mv) > 0U,          \
                      "Motor safety limits must be positive");          \
        static_assert(kPwmFrequency > 0U                                 \
                      && kCurrentBandwidth > 0U                          \
                      && static_cast<uint64_t>(kCurrentBandwidth) * 2U   \
                             < kPwmFrequency,                            \
                      "Current bandwidth must be below PWM Nyquist");   \
        static_assert(kTimerClockHz > 0U                                 \
                      && kPwmPrescaler <= UINT16_MAX                     \
                      && kTimerClockHz / (kPwmPrescaler + 1U)            \
                             >= static_cast<uint64_t>(kPwmFrequency)     \
                                    * 2U,                                \
                      "PWM timer clock or prescaler is invalid");       \
        static_assert(static_cast<uint64_t>(kDeadTimeNs)                 \
                          * kPwmFrequency * 2U < 1000000000ULL,          \
                      "PWM dead time exceeds half a period");           \
        static_assert(kAdcResolution == 6U || kAdcResolution == 8U       \
                      || kAdcResolution == 10U                           \
                      || kAdcResolution == 12U,                          \
                      "ADC resolution is unsupported");                 \
        static_assert(kCurrentUChannel <= 15U                            \
                      && kCurrentWChannel <= 15U                         \
                      && kVbusChannel <= 15U                             \
                      && kCurrentUChannel != kCurrentWChannel            \
                      && kCurrentUChannel != kVbusChannel                \
                      && kCurrentWChannel != kVbusChannel,               \
                      "FOC ADC channels are invalid");                  \
        static_assert(kAdcReferenceMv > 0U                               \
                      && DT_PROP(node_id, current_sense_uv_per_amp) > 0U \
                      && DT_PROP(node_id, vbus_divider_milli) > 0U       \
                      && kCurrentZeroCode < (1U << kAdcResolution),      \
                      "FOC ADC scaling is invalid");                    \
                                                                          \
        using type = foc::MotorDevice<                                   \
            DT_ORD(DT_PHANDLE(node_id, pwm_dev)),                        \
            DT_ORD(DT_PHANDLE(node_id, adc_dev)),                        \
            kChannelU, kChannelV, kChannelW>;                            \
        static type instance;                                            \
                                                                          \
        static int init()                                                 \
        {                                                                 \
            foc::MotorConfig config{};                                    \
            config.rs = static_cast<float>(                               \
                DT_PROP(node_id, rs_milliohm)) * kMilli;                  \
            config.ld = static_cast<float>(                               \
                DT_PROP(node_id, ld_microhenry)) * kMicro;                \
            config.lq = static_cast<float>(                               \
                DT_PROP(node_id, lq_microhenry)) * kMicro;                \
            config.flux_linkage = static_cast<float>(                     \
                DT_PROP(node_id, flux_milliweber)) * kMilli;              \
            config.pole_pairs = static_cast<uint8_t>(kPolePairs);         \
            config.imax = static_cast<float>(                             \
                DT_PROP(node_id, max_current_ma)) * kMilli;               \
            config.vmax = static_cast<float>(                             \
                DT_PROP(node_id, max_voltage_mv)) * kMilli;               \
            config.foc.pwm_frequency = kPwmFrequency;                     \
            config.foc.current_bandwidth = kCurrentBandwidth;             \
            config.timer_clock_hz = kTimerClockHz;                        \
            config.pwm_prescaler = static_cast<uint16_t>(kPwmPrescaler);  \
            config.dead_time_ns = kDeadTimeNs;                            \
            config.adc_resolution_bits =                                 \
                static_cast<uint8_t>(kAdcResolution);                     \
            config.current_u_channel = static_cast<hal::AdcChannel>(      \
                kCurrentUChannel);                                        \
            config.current_w_channel = static_cast<hal::AdcChannel>(      \
                kCurrentWChannel);                                        \
            config.vbus_channel = static_cast<hal::AdcChannel>(           \
                kVbusChannel);                                            \
            config.adc_trigger_source =                                  \
                DT_PROP(node_id, adc_trigger_source);                     \
            config.adc_reference_voltage =                               \
                static_cast<float>(kAdcReferenceMv) * kMilli;             \
            config.current_zero_code =                                   \
                static_cast<float>(kCurrentZeroCode);                     \
            config.current_sense_volts_per_amp = static_cast<float>(      \
                DT_PROP(node_id, current_sense_uv_per_amp)) * kMicro;     \
            config.vbus_divider_ratio = static_cast<float>(               \
                DT_PROP(node_id, vbus_divider_milli)) * kMilli;           \
            config.sensor = static_cast<foc::SensorMode>(kSensorMode);     \
            config.control = static_cast<foc::ControlMode>(kControlMode); \
            return instance.init(config);                                 \
        }                                                                 \
    };
