#pragma once

#include <device.h>
#include <drivers/status.h>
#include <osal.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hal {

enum class MemAddrSize : uint8_t { Bit8 = 0, Bit16 };

struct I2cStats {
    uint32_t transfer_count {0U};
    uint32_t nack_addr_count {0U};
    uint32_t nack_data_count {0U};
    uint32_t bus_error_count {0U};
    uint32_t timeout_count {0U};
};

struct I2cAtomicStats {
    std::atomic<uint32_t> transfer_count {0U};
    std::atomic<uint32_t> nack_addr_count {0U};
    std::atomic<uint32_t> nack_data_count {0U};
    std::atomic<uint32_t> bus_error_count {0U};
    std::atomic<uint32_t> timeout_count {0U};
};

class I2cBase : public DeviceBase {
public:
    using DeviceBase::is_ready;

    [[nodiscard]] Status init(uint32_t timing);
    [[nodiscard]] Status deinit();

    [[nodiscard]] Status master_transmit(
        uint16_t addr, const uint8_t *data, size_t len,
        uint32_t timeout_ms);
    [[nodiscard]] Status master_receive(
        uint16_t addr, uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status mem_write(
        uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
        const uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status mem_read(
        uint16_t addr, uint16_t mem_addr, MemAddrSize addr_size,
        uint8_t *data, size_t len, uint32_t timeout_ms);
    [[nodiscard]] Status probe(uint16_t addr, uint32_t retries,
                               uint32_t timeout_ms);
    void isr_handler(osal::IsrContext &context);

    [[nodiscard]] I2cStats get_stats() const {
        return {
            m_stats.transfer_count.load(std::memory_order_relaxed),
            m_stats.nack_addr_count.load(std::memory_order_relaxed),
            m_stats.nack_data_count.load(std::memory_order_relaxed),
            m_stats.bus_error_count.load(std::memory_order_relaxed),
            m_stats.timeout_count.load(std::memory_order_relaxed),
        };
    }
    void reset_stats() {
        m_stats.transfer_count.store(0U, std::memory_order_relaxed);
        m_stats.nack_addr_count.store(0U, std::memory_order_relaxed);
        m_stats.nack_data_count.store(0U, std::memory_order_relaxed);
        m_stats.bus_error_count.store(0U, std::memory_order_relaxed);
        m_stats.timeout_count.store(0U, std::memory_order_relaxed);
    }

    osal::Mutex &bus_mutex() { return m_bus_mutex; }
    [[nodiscard]] uintptr_t base() const { return m_base; }

protected:
    explicit I2cBase(uintptr_t base) : m_base(base) {}
    uintptr_t m_base;
    osal::Mutex m_bus_mutex;
    I2cAtomicStats m_stats {};
};

template <uintptr_t Base>
class I2c : public I2cBase {
public:
    I2c() : I2cBase(Base) {}
};

template <int BusOrd, uint16_t Addr>
class I2cDevice {
public:
    [[nodiscard]] Status init(uint32_t timing) { return bus().init(timing); }
    [[nodiscard]] bool is_initialized() const
    {
        return bus().is_initialized();
    }

    [[nodiscard]] I2cBase &bus()
    {
        return DeviceTrait<BusOrd>::instance;
    }

    [[nodiscard]] const I2cBase &bus() const
    {
        return DeviceTrait<BusOrd>::instance;
    }

    [[nodiscard]] constexpr uint16_t addr() const { return Addr; }

    [[nodiscard]] Status read(uint8_t *data, size_t len,
                              uint32_t timeout_ms = 100U)
    {
        return bus().master_receive(Addr, data, len, timeout_ms);
    }

    [[nodiscard]] Status write(const uint8_t *data, size_t len,
                               uint32_t timeout_ms = 100U)
    {
        return bus().master_transmit(Addr, data, len, timeout_ms);
    }

    [[nodiscard]] Status mem_read(
        uint16_t mem_addr, MemAddrSize addr_size, uint8_t *data,
        size_t len, uint32_t timeout_ms = 100U)
    {
        return bus().mem_read(
            Addr, mem_addr, addr_size, data, len, timeout_ms);
    }

    [[nodiscard]] Status mem_write(
        uint16_t mem_addr, MemAddrSize addr_size, const uint8_t *data,
        size_t len, uint32_t timeout_ms = 100U)
    {
        return bus().mem_write(
            Addr, mem_addr, addr_size, data, len, timeout_ms);
    }

    [[nodiscard]] Status is_ready(uint32_t retries = 3U,
                                  uint32_t timeout_ms = 10U)
    {
        return bus().probe(Addr, retries, timeout_ms);
    }
};

} // namespace hal
