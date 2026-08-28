#pragma once

#include <device.h>
#include <gimbal/hall_device.h>

#include <cstdint>

#define HAL_GIMBAL_HALL_DT_ADAPT(node_id)                         \
    template <>                                                    \
    struct DeviceTrait<DT_ORD(node_id)> {                          \
        static constexpr uint32_t kChannelA =                      \
            DT_PROP(node_id, channel_a);                           \
        static constexpr uint32_t kChannelB =                      \
            DT_PROP(node_id, channel_b);                           \
        static constexpr uint32_t kSampleTime =                    \
            DT_PROP(node_id, sample_time);                         \
                                                                   \
        static_assert(kChannelA <= 15U && kChannelB <= 15U,        \
                      "Hall ADC channel is invalid");             \
        static_assert(kChannelA != kChannelB,                      \
                      "Hall ADC channels must be unique");        \
        static_assert(kSampleTime <= 7U,                           \
                      "Hall ADC sample time is invalid");         \
                                                                   \
        using type = gimbal::HallAdcDevice<                        \
            DT_ORD(DT_PHANDLE(node_id, adc_dev)),                  \
            static_cast<uint8_t>(kChannelA),                       \
            static_cast<uint8_t>(kChannelB)>;                      \
        static type instance;                                      \
                                                                   \
        static int init()                                          \
        {                                                          \
            return instance.init(                                  \
                static_cast<AdcSampleTime>(kSampleTime));           \
        }                                                          \
    };
