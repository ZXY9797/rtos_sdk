#pragma once

#include <gimbal/hall_sensor.h>

#include <device.h>
#include <drivers/adc.h>

namespace gimbal {

template <int AdcOrdinal, uint8_t ChannelA, uint8_t ChannelB>
class HallAdcDevice {
    static_assert(ChannelA <= 15U);
    static_assert(ChannelB <= 15U);
    static_assert(ChannelA != ChannelB);

public:
    [[nodiscard]] int init(hal::AdcSampleTime sample_time)
    {
        auto &adc = hal::device_get<AdcOrdinal>();
        is_initialized_ = adc.is_initialized()
            && adc.set_sample_time(
                static_cast<hal::AdcChannel>(ChannelA), sample_time)
                == hal::Status::Ok
            && adc.set_sample_time(
                static_cast<hal::AdcChannel>(ChannelB), sample_time)
                == hal::Status::Ok;
        return is_initialized_ ? 0 : -1;
    }

    [[nodiscard]] int deinit()
    {
        is_initialized_ = false;
        return 0;
    }

    [[nodiscard]] bool read(HallRawSample &sample)
    {
        if (!is_initialized_) {
            return false;
        }
        uint16_t channel_a = 0U;
        uint16_t channel_b = 0U;
        auto &adc = hal::device_get<AdcOrdinal>();
        const hal::Status status_a = adc.read(
            static_cast<hal::AdcChannel>(ChannelA), channel_a);
        const hal::Status status_b = adc.read(
            static_cast<hal::AdcChannel>(ChannelB), channel_b);
        if (status_a != hal::Status::Ok || status_b != hal::Status::Ok) {
            return false;
        }
        sample.channel_a = static_cast<float>(channel_a);
        sample.channel_b = static_cast<float>(channel_b);
        return true;
    }

    [[nodiscard]] bool is_initialized() const { return is_initialized_; }

private:
    bool is_initialized_ {false};
};

} // namespace gimbal
