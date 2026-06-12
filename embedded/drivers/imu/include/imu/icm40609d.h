#pragma once

#include <drivers/gpio.h>
#include <drivers/spi.h>
#include <cstddef>
#include <cstdint>

namespace imu {

struct ImuConfig {
    uint8_t accel_fs {0};
    uint8_t gyro_fs {0};
    uint16_t sample_rate {16000};
};

struct ImuData {
    int16_t accel[3];
    int16_t gyro[3];
    int16_t temp;
};

/// ICM40609D 6-axis IMU driver using a generated SPI bus device.
///
/// Template parameters:
///   SpiBusOrd  - SPI bus DeviceTrait ordinal.
///   CsPortBase - CS GPIO port base address.
///   CsPin      - CS pin number.
template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin>
class Icm40609d {
    static_assert(CsPin < 32U, "CS pin out of register bit range");

public:
    Icm40609d() = default;

    int init(const ImuConfig &cfg);

    bool read(ImuData &data);

    bool is_initialized() const { return initialized_; }

private:
    void cs_low();
    void cs_high();
    uint8_t read_reg(uint8_t reg);
    void write_reg(uint8_t reg, uint8_t val);
    bool read_burst(uint8_t reg, uint8_t *buf, size_t len);

    static int16_t be16(const uint8_t *buf) {
        const uint16_t value = (static_cast<uint16_t>(buf[0]) << 8U) |
                               static_cast<uint16_t>(buf[1]);
        return static_cast<int16_t>(value);
    }

    hal::SpiDevice<SpiBusOrd, 0xFF> spi_;
    bool initialized_ {false};

    static constexpr uint8_t REG_WHO_AM_I      = 0x75U;
    static constexpr uint8_t REG_PWR_MGMT0     = 0x4EU;
    static constexpr uint8_t REG_GYRO_CONFIG0  = 0x4FU;
    static constexpr uint8_t REG_ACCEL_CONFIG0 = 0x50U;
    static constexpr uint8_t REG_ACCEL_DATA    = 0x1FU;
    static constexpr uint8_t WHO_AM_I_VAL      = 0x3FU;
    static constexpr uint8_t READ_BIT          = 0x80U;

    static constexpr size_t kSamplePayloadBytes = 14U;
    static constexpr size_t kMaxBurstPayloadBytes = kSamplePayloadBytes;

    static constexpr uintptr_t GPIO_BOP_OFFSET = 0x18U;
    static constexpr uintptr_t GPIO_BC_OFFSET  = 0x28U;
};

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin>
void Icm40609d<SpiBusOrd, CsPortBase, CsPin>::cs_low()
{
    auto *bc = reinterpret_cast<volatile uint32_t *>(
        CsPortBase + GPIO_BC_OFFSET);
    *bc = 1UL << CsPin;
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin>
void Icm40609d<SpiBusOrd, CsPortBase, CsPin>::cs_high()
{
    auto *bop = reinterpret_cast<volatile uint32_t *>(
        CsPortBase + GPIO_BOP_OFFSET);
    *bop = 1UL << CsPin;
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin>
uint8_t Icm40609d<SpiBusOrd, CsPortBase, CsPin>::read_reg(uint8_t reg)
{
    uint8_t tx[2] = {static_cast<uint8_t>(reg | READ_BIT), 0U};
    uint8_t rx[2] = {};

    cs_low();
    const hal::Status status = spi_.bus().sync_send(tx, rx, sizeof(tx), 10U);
    cs_high();

    return status == hal::Status::Ok ? rx[1] : 0xFFU;
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin>
void Icm40609d<SpiBusOrd, CsPortBase, CsPin>::write_reg(uint8_t reg,
                                                        uint8_t val)
{
    uint8_t tx[2] = {reg, val};
    uint8_t rx[2] = {};

    cs_low();
    (void)spi_.bus().sync_send(tx, rx, sizeof(tx), 10U);
    cs_high();
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin>
bool Icm40609d<SpiBusOrd, CsPortBase, CsPin>::read_burst(uint8_t reg,
                                                         uint8_t *buf,
                                                         size_t len)
{
    if (buf == nullptr || len > kMaxBurstPayloadBytes) {
        return false;
    }

    uint8_t tx[kMaxBurstPayloadBytes + 1U] = {};
    uint8_t rx[kMaxBurstPayloadBytes + 1U] = {};
    tx[0] = static_cast<uint8_t>(reg | READ_BIT);

    cs_low();
    const hal::Status status = spi_.bus().sync_send(tx, rx, len + 1U, 10U);
    cs_high();

    if (status != hal::Status::Ok) {
        return false;
    }

    for (size_t i = 0; i < len; i++) {
        buf[i] = rx[i + 1U];
    }

    return true;
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin>
int Icm40609d<SpiBusOrd, CsPortBase, CsPin>::init(const ImuConfig &cfg)
{
    hal::SpiConfig spi_cfg;
    spi_cfg.mode = hal::SpiMode::Mode0;
    spi_cfg.clock_hz = 8000000U;

    if (spi_.init(spi_cfg) != hal::Status::Ok) {
        return -1;
    }

    hal::GpioPortBase cs_port(CsPortBase);
    const int gpio_ret = cs_port.configure(CsPin, GPIO_OUTPUT_HIGH);
    if (gpio_ret != 0) {
        return gpio_ret;
    }

    const uint8_t who = read_reg(REG_WHO_AM_I);
    if (who != WHO_AM_I_VAL) {
        return -1;
    }

    write_reg(REG_PWR_MGMT0, 0x00U);
    write_reg(REG_GYRO_CONFIG0,
              static_cast<uint8_t>((cfg.gyro_fs & 0x03U) << 5U));
    write_reg(REG_ACCEL_CONFIG0,
              static_cast<uint8_t>((cfg.accel_fs & 0x03U) << 5U));
    write_reg(REG_PWR_MGMT0, 0x0FU);

    initialized_ = true;
    return 0;
}

template <int SpiBusOrd, uintptr_t CsPortBase, uint8_t CsPin>
bool Icm40609d<SpiBusOrd, CsPortBase, CsPin>::read(ImuData &data)
{
    uint8_t buf[kSamplePayloadBytes] = {};
    if (!read_burst(REG_ACCEL_DATA, buf, sizeof(buf))) {
        return false;
    }

    data.accel[0] = be16(&buf[0]);
    data.accel[1] = be16(&buf[2]);
    data.accel[2] = be16(&buf[4]);
    data.temp     = be16(&buf[6]);
    data.gyro[0]  = be16(&buf[8]);
    data.gyro[1]  = be16(&buf[10]);
    data.gyro[2]  = be16(&buf[12]);

    return true;
}

} // namespace imu
