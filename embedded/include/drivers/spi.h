#pragma once

#include <device.h>
#include <drivers/status.h>
#include <osal.h>
#include <cstdint>
#include <cstddef>

namespace hal {

enum class SpiMode : uint8_t { Mode0 = 0, Mode1, Mode2, Mode3 };

struct SpiConfig {
    SpiMode mode {SpiMode::Mode0};
    uint32_t clock_hz {1000000U};
    uint8_t data_bits {8};
};

/// SPI 运行时统计
struct SpiStats {
    uint32_t xfer_count {0};      ///< 传输次数
    uint32_t xfer_bytes {0};      ///< 传输字节数
    uint32_t error_count {0};     ///< 错误次数
    uint32_t timeout_count {0};   ///< 超时次数
};

class SpiBase : public DeviceBase {
public:
    [[nodiscard]] Status init(const SpiConfig &config);
    [[nodiscard]] Status deinit();

    [[nodiscard]] Status sync_send(const uint8_t *tx, uint8_t *rx, size_t len, uint32_t timeout_ms);

    /// 获取运行时统计
    [[nodiscard]] SpiStats get_stats() const { return m_stats; }

    /// 重置统计计数器
    void reset_stats() { m_stats = {}; }

    /// 获取总线互斥锁引用（供 SpiDevice 使用）
    osal::Mutex &bus_mutex() { return m_bus_mutex; }

    /// 获取传输完成信号量引用（供 ISR 使用）
    osal::Semaphore &xfer_sem() { return m_xfer_sem; }

    /// 获取基地址（供 ISR 分发使用）
    uintptr_t base() const { return m_base; }

protected:
    explicit SpiBase(uintptr_t base) : m_base(base) {}
    uintptr_t m_base;
    osal::Mutex m_bus_mutex;
    osal::Semaphore m_xfer_sem {0};
    SpiStats m_stats {};
};

template <uintptr_t Base>
class Spi : public SpiBase {
public:
    Spi() : SpiBase(Base) {}
};

/// SPI 设备（挂在总线上的从设备，支持多设备共享总线）
template <int BusOrd, uint8_t CsIndex = 0xFF>
class SpiDevice {
public:
    [[nodiscard]] Status init(const SpiConfig &config) {
        m_config = config;
        m_initialized = true;
        return Status::Ok;
    }

    [[nodiscard]] bool is_initialized() const { return m_initialized; }

    /// 获取总线实例引用
    [[nodiscard]] SpiBase &bus() {
        return DeviceTrait<BusOrd>::instance;
    }

    /// 获取配置
    [[nodiscard]] const SpiBase &bus() const {
        return DeviceTrait<BusOrd>::instance;
    }

    [[nodiscard]] const SpiConfig &config() const { return m_config; }

    /// 获取 CS 索引
    [[nodiscard]] constexpr uint8_t cs_index() const { return CsIndex; }

private:
    SpiConfig m_config {};
    bool m_initialized {false};
};

} // namespace hal
