#pragma once

#include <drivers/status.h>
#include <device_base.h>
#include <osal.h>
#include <cstdint>
#include <cstddef>

namespace hal {

enum class MemAddrSize : uint8_t { Bit8 = 0, Bit16 };

/// I2C 运行时统计
struct I2cStats {
    uint32_t transfer_count {0};    ///< 传输次数
    uint32_t nack_addr_count {0};   ///< 地址 NACK 次数
    uint32_t nack_data_count {0};   ///< 数据 NACK 次数
    uint32_t bus_error_count {0};   ///< 总线错误次数
    uint32_t timeout_count {0};     ///< 超时次数
};

class I2cBase : public DeviceBase {
public:
    [[nodiscard]] Status init(uint32_t timing);
    [[nodiscard]] Status deinit();

    [[nodiscard]] Status master_transmit(uint16_t addr, const uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status master_receive(uint16_t addr, uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status mem_write(uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
                                   const uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status mem_read(uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
                                  uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status is_ready(uint16_t addr, uint32_t retries, uint32_t timeout_ms);

    /// 获取运行时统计
    [[nodiscard]] I2cStats get_stats() const { return m_stats; }

    /// 获取可变统计引用（供 ISR 使用）
    I2cStats &stats() { return m_stats; }

    /// 重置统计计数器
    void reset_stats() { m_stats = {}; }

    /// 获取总线互斥锁引用（供 I2cDevice 使用）
    osal::Mutex &bus_mutex() { return m_bus_mutex; }

    /// 获取传输完成信号量引用（供 ISR 使用）
    osal::Semaphore &xfer_sem() { return m_xfer_sem; }

    /// 获取基地址（供 ISR 分发使用）
    uintptr_t base() const { return m_base; }

protected:
    explicit I2cBase(uintptr_t base) : m_base(base) {}
    uintptr_t m_base;
    osal::Mutex m_bus_mutex;
    osal::Semaphore m_xfer_sem {0};
    I2cStats m_stats {};
};

template <uintptr_t Base>
class I2c : public I2cBase {
public:
    I2c() : I2cBase(Base) {}
};

/// I2C 设备（挂在总线上的从设备，支持多设备共享总线）
template <uintptr_t BusBase, uint16_t Addr>
class I2cDevice {
public:
    [[nodiscard]] Status init(uint32_t timing) {
        return bus().init(timing);
    }

    [[nodiscard]] bool is_initialized() const {
        return bus().is_initialized();
    }

    /// 获取总线实例引用
    [[nodiscard]] I2cBase &bus() {
        return *reinterpret_cast<I2cBase *>(BusBase);
    }

    /// 获取从机地址
    [[nodiscard]] constexpr uint16_t addr() const { return Addr; }

    /// 读取数据
    [[nodiscard]] Status read(uint8_t *data, size_t len,
                              uint32_t timeout_ms = 100) {
        return bus().master_receive(Addr, data, len, timeout_ms);
    }

    /// 写入数据
    [[nodiscard]] Status write(const uint8_t *data, size_t len,
                               uint32_t timeout_ms = 100) {
        return bus().master_transmit(Addr, data, len, timeout_ms);
    }

    /// 内存读取（EEPROM 风格）
    [[nodiscard]] Status mem_read(uint16_t mem_addr, MemAddrSize addr_size,
                                  uint8_t *data, size_t len,
                                  uint32_t timeout_ms = 100) {
        return bus().mem_read(Addr, mem_addr, addr_size, data, len, timeout_ms);
    }

    /// 内存写入（EEPROM 风格）
    [[nodiscard]] Status mem_write(uint16_t mem_addr, MemAddrSize addr_size,
                                   const uint8_t *data, size_t len,
                                   uint32_t timeout_ms = 100) {
        return bus().mem_write(Addr, mem_addr, addr_size, data, len, timeout_ms);
    }

    /// 探测设备是否就绪
    [[nodiscard]] Status is_ready(uint32_t retries = 3,
                                  uint32_t timeout_ms = 10) {
        return bus().is_ready(Addr, retries, timeout_ms);
    }
};

} // namespace hal
