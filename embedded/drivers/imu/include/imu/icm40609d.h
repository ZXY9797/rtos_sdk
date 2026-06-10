#pragma once

#include <drivers/spi.h>
#include <cstdint>

namespace imu {

struct ImuConfig {
    uint8_t accel_fs {0};       // 0=±2g, 1=±4g, 2=±8g, 3=±16g
    uint8_t gyro_fs {0};        // 0=±2000, 1=±1000, 2=±500, 3=±250 dps
    uint16_t sample_rate {16000};
};

struct ImuData {
    int16_t accel[3];   // x, y, z
    int16_t gyro[3];    // x, y, z
    int16_t temp;
};

/// ICM40609D 6 轴 IMU 驱动（SPI 接口）
///
/// 模板参数:
///   SpiBusBase  - SPI 总线控制器基地址 (如 0x40013000 = SPI0)
///   CsPortBase  - CS 引脚所在 GPIO 端口基地址 (如 GPIOB_BASE)
///   CsPin       - CS 引脚号 (0-15)
template <uintptr_t SpiBusBase, uintptr_t CsPortBase, uint8_t CsPin>
class Icm40609d {
public:
    Icm40609d() = default;

    int init(const ImuConfig &cfg);

    /// ISR 上下文读取 IMU 数据（SPI burst read，~12μs）
    bool read(ImuData &data);

    bool is_initialized() const { return initialized_; }

private:
    void cs_low();
    void cs_high();
    uint8_t read_reg(uint8_t reg);
    void write_reg(uint8_t reg, uint8_t val);
    bool read_burst(uint8_t reg, uint8_t *buf, size_t len);

    hal::SpiDevice<SpiBusBase, 0xFF> spi_;
    bool initialized_ {false};

    // ICM40609D 寄存器
    static constexpr uint8_t REG_WHO_AM_I      = 0x75;
    static constexpr uint8_t REG_PWR_MGMT0     = 0x4E;
    static constexpr uint8_t REG_GYRO_CONFIG0  = 0x4F;
    static constexpr uint8_t REG_ACCEL_CONFIG0 = 0x50;
    static constexpr uint8_t REG_INTF_CONFIG0  = 0x4C;
    static constexpr uint8_t REG_ACCEL_DATA    = 0x1F;  // burst: ACCEL_XOUT_H .. GYRO_ZOUT_L (14 bytes)
    static constexpr uint8_t WHO_AM_I_VAL      = 0x3F;
    static constexpr uint8_t READ_BIT          = 0x80;

    // GPIO 寄存器偏移
    static constexpr uintptr_t GPIO_BOP_OFFSET = 0x18;
    static constexpr uintptr_t GPIO_BC_OFFSET  = 0x28;
};

// ─── 模板实现 ──────────────────────────────────────────────────

template <uintptr_t SpiBusBase, uintptr_t CsPortBase, uint8_t CsPin>
void Icm40609d<SpiBusBase, CsPortBase, CsPin>::cs_low()
{
    auto *bop = reinterpret_cast<volatile uint32_t *>(CsPortBase + GPIO_BC_OFFSET);
    *bop = 1U << CsPin;
}

template <uintptr_t SpiBusBase, uintptr_t CsPortBase, uint8_t CsPin>
void Icm40609d<SpiBusBase, CsPortBase, CsPin>::cs_high()
{
    auto *bop = reinterpret_cast<volatile uint32_t *>(CsPortBase + GPIO_BOP_OFFSET);
    *bop = 1U << CsPin;
}

template <uintptr_t SpiBusBase, uintptr_t CsPortBase, uint8_t CsPin>
uint8_t Icm40609d<SpiBusBase, CsPortBase, CsPin>::read_reg(uint8_t reg)
{
    uint8_t tx[2] = {static_cast<uint8_t>(reg | READ_BIT), 0};
    uint8_t rx[2] = {};
    cs_low();
    (void)spi_.bus().sync_send(tx, rx, 2, 10);
    cs_high();
    return rx[1];
}

template <uintptr_t SpiBusBase, uintptr_t CsPortBase, uint8_t CsPin>
void Icm40609d<SpiBusBase, CsPortBase, CsPin>::write_reg(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = {reg, val};
    uint8_t rx[2] = {};
    cs_low();
    (void)spi_.bus().sync_send(tx, rx, 2, 10);
    cs_high();
}

template <uintptr_t SpiBusBase, uintptr_t CsPortBase, uint8_t CsPin>
bool Icm40609d<SpiBusBase, CsPortBase, CsPin>::read_burst(uint8_t reg, uint8_t *buf, size_t len)
{
    uint8_t tx[15] = {};
    uint8_t rx[15] = {};
    tx[0] = reg | READ_BIT;
    cs_low();
    auto st = spi_.bus().sync_send(tx, rx, len + 1, 10);
    cs_high();
    if (st != hal::Status::Ok) return false;
    for (size_t i = 0; i < len; i++) buf[i] = rx[i + 1];
    return true;
}

template <uintptr_t SpiBusBase, uintptr_t CsPortBase, uint8_t CsPin>
int Icm40609d<SpiBusBase, CsPortBase, CsPin>::init(const ImuConfig &cfg)
{
    hal::SpiConfig spi_cfg;
    spi_cfg.mode = hal::SpiMode::Mode0;
    spi_cfg.clock_hz = 8000000;
    (void)spi_.init(spi_cfg);

    // CS 引脚配置为推挽输出，默认高电平
    hal::GpioPortBase cs_port(CsPortBase);
    (void)cs_port.configure(CsPin, GPIO_OUTPUT_HIGH);

    // 验证 WHO_AM_I
    uint8_t who = read_reg(REG_WHO_AM_I);
    if (who != WHO_AM_I_VAL) return -1;

    // 软复位
    write_reg(REG_PWR_MGMT0, 0x00);

    // 配置量程
    write_reg(REG_GYRO_CONFIG0, (cfg.gyro_fs & 0x03) << 5);
    write_reg(REG_ACCEL_CONFIG0, (cfg.accel_fs & 0x03) << 5);

    // 使能 gyro + accel (低噪声模式)
    write_reg(REG_PWR_MGMT0, 0x0F);

    initialized_ = true;
    return 0;
}

template <uintptr_t SpiBusBase, uintptr_t CsPortBase, uint8_t CsPin>
bool Icm40609d<SpiBusBase, CsPortBase, CsPin>::read(ImuData &data)
{
    uint8_t buf[14];
    if (!read_burst(REG_ACCEL_DATA, buf, 14)) return false;

    data.accel[0] = static_cast<int16_t>((buf[0] << 8) | buf[1]);
    data.accel[1] = static_cast<int16_t>((buf[2] << 8) | buf[3]);
    data.accel[2] = static_cast<int16_t>((buf[4] << 8) | buf[5]);
    data.temp     = static_cast<int16_t>((buf[6] << 8) | buf[7]);
    data.gyro[0]  = static_cast<int16_t>((buf[8] << 8) | buf[9]);
    data.gyro[1]  = static_cast<int16_t>((buf[10] << 8) | buf[11]);
    data.gyro[2]  = static_cast<int16_t>((buf[12] << 8) | buf[13]);
    return true;
}

} // namespace imu
