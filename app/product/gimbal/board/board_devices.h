#pragma once

#include <drivers_generated.h>

#include <array>
#include <cstdint>

namespace app::board {

inline constexpr uint32_t kHardwareRevision = 1U;

struct MotorPwmTopology {
    uint32_t period_counts;
    hal::PwmChannel phase_u;
    hal::PwmChannel phase_v;
    hal::PwmChannel phase_w;
};

#define GIMBAL_MOTOR_TOPOLOGY(alias)                              \
    MotorPwmTopology {                                            \
        hal::DeviceTrait<DT_ORD(DT_ALIAS(alias))>::kPeriod,       \
        static_cast<hal::PwmChannel>(                             \
            hal::DeviceTrait<DT_ORD(DT_ALIAS(alias))>::kChannelU),\
        static_cast<hal::PwmChannel>(                             \
            hal::DeviceTrait<DT_ORD(DT_ALIAS(alias))>::kChannelV),\
        static_cast<hal::PwmChannel>(                             \
            hal::DeviceTrait<DT_ORD(DT_ALIAS(alias))>::kChannelW),\
    }

inline constexpr std::array<MotorPwmTopology, 3U> kMotorTopologies {
    GIMBAL_MOTOR_TOPOLOGY(motor_roll),
    GIMBAL_MOTOR_TOPOLOGY(motor_pitch),
    GIMBAL_MOTOR_TOPOLOGY(motor_yaw),
};

#undef GIMBAL_MOTOR_TOPOLOGY

inline constexpr uint32_t kHeaterPwmPeriodCounts =
    hal::DeviceTrait<DT_ORD(DT_ALIAS(heater_pwm))>::kPeriod;

inline constexpr uint32_t kSensorTimerInputHz =
    CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC;
inline constexpr uint32_t kSensorTimerHz = kSensorTimerInputHz
    / (hal::DeviceTrait<DT_ORD(DT_ALIAS(sensor_trigger))>::kPrescaler + 1U)
    / (hal::DeviceTrait<DT_ORD(DT_ALIAS(sensor_trigger))>::kPeriod + 1U);
static_assert(kSensorTimerHz == CONFIG_GIMBAL_SENSOR_HZ,
              "Sensor timer and Kconfig sample rates differ");
static_assert(hal::DeviceTrait<DT_ORD(DT_ALIAS(imu0))>::kSampleRate
                  == CONFIG_GIMBAL_SENSOR_HZ,
              "IMU ODR and Kconfig sample rates differ");

decltype(device_get(uart0)) console();
decltype(device_get(imu0)) imu();
decltype(device_get(hall_roll)) roll_hall();
decltype(device_get(hall_pitch)) pitch_hall();
decltype(device_get(hall_yaw)) yaw_hall();
decltype(device_get(motor_roll)) roll_motor();
decltype(device_get(motor_pitch)) pitch_motor();
decltype(device_get(motor_yaw)) yaw_motor();
decltype(device_get(heater_pwm)) imu_heater_pwm();
decltype(device_get(sensor_trigger)) sensor_timer();

} // namespace app::board
