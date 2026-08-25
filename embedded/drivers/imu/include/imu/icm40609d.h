#pragma once

#include <drivers/gpio.h>
#include <drivers/spi.h>

#include <cstddef>
#include <cstdint>

namespace imu {

namespace detail {

// ICM-40609-D GYRO_CONFIG0/ACCEL_CONFIG0 ODR encoding (bits 3:0).
constexpr uint8_t icm40609d_odr_code(uint16_t sample_rate_hz)
{
    switch (sample_rate_hz) {
    case 32000U: return 0x01U;
    case 16000U: return 0x02U;
    case 8000U: return 0x03U;
    case 4000U: return 0x04U;
    case 2000U: return 0x05U;
    case 1000U: return 0x06U;
    case 500U: return 0x0FU;
    case 200U: return 0x07U;
    case 100U: return 0x08U;
    case 50U: return 0x09U;
    case 25U: return 0x0AU;
    default: return 0U;
    }
}

} // namespace detail

struct ImuConfig {
    uint8_t accel_fs {0U};
    uint8_t gyro_fs {0U};
    uint16_t sample_rate {16000U};
    uint32_t spi_clock_hz {10000000U};
    hal::SpiMode spi_mode {hal::SpiMode::Mode0};
};

struct ImuData {
    int16_t accel[3] {};
    int16_t gyro[3] {};
    int16_t temp {};
};

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin,
          uint32_t CsFlags>
class Icm40609d {
    static_assert(CsPin < 32U);

public:
    Icm40609d() = default;
    int init(const ImuConfig& cfg);
    int deinit();
    bool read(ImuData& data);
    [[nodiscard]] bool is_initialized() const { return initialized_; }

private:
    static void chip_select(void* arg, bool active)
    {
        auto* const self = static_cast<Icm40609d*>(arg);
        if (active) {
            self->cs_.on();
        } else {
            self->cs_.off();
        }
    }

    bool read_reg(uint8_t reg, uint8_t& value);
    bool write_reg(uint8_t reg, uint8_t value);
    bool read_burst(uint8_t reg, uint8_t* buf, size_t len);

    static int16_t be16(const uint8_t* buf)
    {
        const uint16_t value = (static_cast<uint16_t>(buf[0]) << 8U)
                             | static_cast<uint16_t>(buf[1]);
        return static_cast<int16_t>(value);
    }

    hal::SpiDevice<SpiBusOrd, 0xFFU> spi_;
    hal::GpioPort<CsPortBase, CsPin, CsFlags> cs_;
    bool initialized_ {false};

    static constexpr uint8_t REG_WHO_AM_I = 0x75U;
    static constexpr uint8_t REG_PWR_MGMT0 = 0x4EU;
    static constexpr uint8_t REG_GYRO_CONFIG0 = 0x4FU;
    static constexpr uint8_t REG_ACCEL_CONFIG0 = 0x50U;
    static constexpr uint8_t REG_ACCEL_DATA = 0x1FU;
    static constexpr uint8_t WHO_AM_I_VAL = 0x3FU;
    static constexpr uint8_t READ_BIT = 0x80U;
    static constexpr size_t kSamplePayloadBytes = 14U;
    static constexpr size_t kMaxBurstPayloadBytes = kSamplePayloadBytes;
};

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin, uint32_t CsFlags>
bool Icm40609d<SpiBusOrd, CsPortBase, CsPin, CsFlags>::read_reg(
    uint8_t reg, uint8_t& value)
{
    const uint8_t tx[2] = {static_cast<uint8_t>(reg | READ_BIT), 0xFFU};
    uint8_t rx[2] {};
    const hal::Status status = spi_.transfer(tx, rx, sizeof(tx), 10U);
    if (status != hal::Status::Ok) {
        return false;
    }
    value = rx[1];
    return true;
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin, uint32_t CsFlags>
bool Icm40609d<SpiBusOrd, CsPortBase, CsPin, CsFlags>::write_reg(
    uint8_t reg, uint8_t value)
{
    const uint8_t tx[2] = {reg, value};
    return spi_.transfer(tx, nullptr, sizeof(tx), 10U) == hal::Status::Ok;
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin, uint32_t CsFlags>
bool Icm40609d<SpiBusOrd, CsPortBase, CsPin, CsFlags>::read_burst(
    uint8_t reg, uint8_t* buf, size_t len)
{
    if (buf == nullptr || len == 0U || len > kMaxBurstPayloadBytes) {
        return false;
    }
    uint8_t tx[kMaxBurstPayloadBytes + 1U] {};
    uint8_t rx[kMaxBurstPayloadBytes + 1U] {};
    tx[0] = static_cast<uint8_t>(reg | READ_BIT);
    for (size_t i = 1U; i <= len; ++i) {
        tx[i] = 0xFFU;
    }
    if (spi_.transfer(tx, rx, len + 1U, 10U) != hal::Status::Ok) {
        return false;
    }
    for (size_t i = 0U; i < len; ++i) {
        buf[i] = rx[i + 1U];
    }
    return true;
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin, uint32_t CsFlags>
int Icm40609d<SpiBusOrd, CsPortBase, CsPin, CsFlags>::init(const ImuConfig& cfg)
{
    initialized_ = false;
    const uint8_t odr = detail::icm40609d_odr_code(cfg.sample_rate);
    if (cfg.accel_fs > 3U || cfg.gyro_fs > 3U || odr == 0U
        || cfg.spi_clock_hz == 0U) {
        return -1;
    }

    constexpr bool active_low = (CsFlags & GPIO_ACTIVE_LOW) != 0U;
    const uint32_t initial_flags = active_low ? GPIO_OUTPUT_HIGH : GPIO_OUTPUT_LOW;
    if (cs_.configure(initial_flags) != 0) {
        return -2;
    }
    cs_.off();

    hal::SpiConfig spi_config {};
    spi_config.mode = cfg.spi_mode;
    spi_config.clock_hz = cfg.spi_clock_hz;
    spi_config.data_bits = 8U;
    if (spi_.init(spi_config) != hal::Status::Ok) {
        return -3;
    }
    spi_.set_chip_select(chip_select, this);

    uint8_t who_am_i = 0U;
    if (!read_reg(REG_WHO_AM_I, who_am_i) || who_am_i != WHO_AM_I_VAL) {
        return -4;
    }
    if (!write_reg(REG_PWR_MGMT0, 0x00U)
        || !write_reg(REG_GYRO_CONFIG0,
                      static_cast<uint8_t>(((cfg.gyro_fs & 0x03U) << 5U)
                                           | odr))
        || !write_reg(REG_ACCEL_CONFIG0,
                      static_cast<uint8_t>(((cfg.accel_fs & 0x03U) << 5U)
                                           | odr))
        || !write_reg(REG_PWR_MGMT0, 0x0FU)) {
        return -5;
    }

    initialized_ = true;
    return 0;
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin, uint32_t CsFlags>
int Icm40609d<SpiBusOrd, CsPortBase, CsPin, CsFlags>::deinit()
{
    initialized_ = false;
    cs_.off();
    return static_cast<int>(spi_.deinit());
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin, uint32_t CsFlags>
bool Icm40609d<SpiBusOrd, CsPortBase, CsPin, CsFlags>::read(ImuData& data)
{
    if (!initialized_) {
        return false;
    }
    uint8_t buf[kSamplePayloadBytes] {};
    if (!read_burst(REG_ACCEL_DATA, buf, sizeof(buf))) {
        return false;
    }
    data.accel[0] = be16(&buf[0]);
    data.accel[1] = be16(&buf[2]);
    data.accel[2] = be16(&buf[4]);
    data.temp = be16(&buf[6]);
    data.gyro[0] = be16(&buf[8]);
    data.gyro[1] = be16(&buf[10]);
    data.gyro[2] = be16(&buf[12]);
    return true;
}

} // namespace imu
