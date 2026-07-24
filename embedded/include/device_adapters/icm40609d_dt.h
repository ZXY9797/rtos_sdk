#pragma once

#include <device.h>
#include <devicetree/gpio.h>
#include <imu/icm40609d.h>
#include <cstdint>

#define HAL_ICM40609D_DT_ADAPT(node_id)                            \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kAccelFs =                       \
            DT_PROP_OR(node_id, accel_fs, 0U);                     \
        static constexpr uint32_t kGyroFs =                        \
            DT_PROP_OR(node_id, gyro_fs, 0U);                      \
        static constexpr uint32_t kSampleRate =                    \
            DT_PROP_OR(node_id, sample_rate, 16000U);              \
                                                                   \
        static_assert(kAccelFs <= 3U,                              \
                      "IMU accelerometer range is invalid");       \
        static_assert(kGyroFs <= 3U,                               \
                      "IMU gyroscope range is invalid");           \
        static_assert(kSampleRate > 0U && kSampleRate <=           \
                      UINT16_MAX,                                  \
                      "IMU sample rate does not fit uint16_t");    \
                                                                   \
        using type = imu::Icm40609d<                              \
            DT_ORD(DT_PARENT(node_id)),                            \
            DT_REG_ADDR(DT_GPIO_CTLR(node_id, cs_gpios)),          \
            DT_GPIO_PIN(node_id, cs_gpios)>;                       \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            imu::ImuConfig config{};                               \
            config.accel_fs = static_cast<uint8_t>(kAccelFs);      \
            config.gyro_fs = static_cast<uint8_t>(kGyroFs);        \
            config.sample_rate =                                  \
                static_cast<uint16_t>(kSampleRate);                \
            return instance.init(config);                          \
        }                                                          \
    };
