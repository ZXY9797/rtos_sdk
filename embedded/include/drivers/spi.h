#pragma once

#include <device.h>
#include <drivers/dma.h>
#include <drivers/status.h>
#include <osal.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hal {

enum class SpiMode : uint8_t { Mode0 = 0U, Mode1, Mode2, Mode3 };

struct SpiConfig {
    SpiMode mode {SpiMode::Mode0};
    uint32_t clock_hz {1000000U};
    uint8_t data_bits {8U};
    DmaChannelConfig dma_tx {};
    DmaChannelConfig dma_rx {};
};

struct SpiStats {
    uint32_t xfer_count {0U};
    uint32_t xfer_bytes {0U};
    uint32_t error_count {0U};
    uint32_t timeout_count {0U};
};

class SpiBase : public DeviceBase {
public:
    using ChipSelectFn = void (*)(void* arg, bool active);

    [[nodiscard]] Status init(const SpiConfig& config);
    [[nodiscard]] Status deinit();
    [[nodiscard]] Status transfer(const uint8_t* tx, uint8_t* rx, size_t len,
                                  uint32_t timeout_ms,
                                  ChipSelectFn chip_select = nullptr,
                                  void* chip_select_arg = nullptr);
    [[nodiscard]] Status sync_send(const uint8_t* tx, uint8_t* rx, size_t len,
                                   uint32_t timeout_ms)
    {
        return transfer(tx, rx, len, timeout_ms, nullptr, nullptr);
    }

    void isr_handler(osal::IsrContext& context);
    void dma_tx_isr(osal::IsrContext& context);
    void dma_rx_isr(osal::IsrContext& context);

    [[nodiscard]] bool read_stats(
        SpiStats& stats, uint32_t timeout_ms = 0U) const {
        osal::LockGuard lock(m_bus_mutex, timeout_ms);
        if (!lock.owns_lock()) {
            return false;
        }
        stats = m_stats;
        return true;
    }
    [[nodiscard]] SpiStats get_stats() const {
        SpiStats stats {};
        (void)read_stats(stats);
        return stats;
    }
    void reset_stats() {
        osal::LockGuard lock(m_bus_mutex, 0U);
        if (lock.owns_lock()) {
            m_stats = {};
        }
    }
    [[nodiscard]] const SpiConfig& config() const { return m_config; }
    osal::Mutex& bus_mutex() { return m_bus_mutex; }
    osal::Semaphore& xfer_sem() { return m_xfer_sem; }
    [[nodiscard]] uintptr_t base() const { return m_base; }

protected:
    explicit SpiBase(uintptr_t base) : m_base(base) {}

    uintptr_t m_base;
    mutable osal::Mutex m_bus_mutex;
    osal::Semaphore m_xfer_sem {0U, 2U};
    SpiStats m_stats {};
    SpiConfig m_config {};
    DmaChannelConfig m_dma_tx {};
    DmaChannelConfig m_dma_rx {};
    std::atomic<uint8_t> m_dma_done_mask {0U};
    std::atomic<uint8_t> m_dma_error_mask {0U};
};

template <uintptr_t Base>
class Spi : public SpiBase {
public:
    Spi() : SpiBase(Base) {}
};

template <int BusOrd, uint8_t CsIndex = 0xFFU>
class SpiDevice {
public:
    [[nodiscard]] Status init(const SpiConfig& config)
    {
        SpiBase& controller = bus();
        if (!controller.is_initialized() || config.clock_hz == 0U
            || config.data_bits != 8U) {
            return Status::InvalidArgument;
        }
        const SpiConfig& bus_config = controller.config();
        if (config.mode != bus_config.mode || config.data_bits != bus_config.data_bits
            || config.clock_hz != bus_config.clock_hz) {
            return Status::NotSupported;
        }
        m_config = config;
        m_initialized = true;
        return Status::Ok;
    }

    [[nodiscard]] Status deinit()
    {
        m_initialized = false;
        m_chip_select = nullptr;
        m_chip_select_arg = nullptr;
        return Status::Ok;
    }

    void set_chip_select(SpiBase::ChipSelectFn fn, void* arg)
    {
        m_chip_select = fn;
        m_chip_select_arg = arg;
    }

    [[nodiscard]] Status transfer(const uint8_t* tx, uint8_t* rx, size_t len,
                                  uint32_t timeout_ms)
    {
        if (!m_initialized || m_chip_select == nullptr) {
            return Status::InvalidArgument;
        }
        return bus().transfer(tx, rx, len, timeout_ms,
                              m_chip_select, m_chip_select_arg);
    }

    [[nodiscard]] bool is_initialized() const { return m_initialized; }
    [[nodiscard]] SpiBase& bus() { return DeviceTrait<BusOrd>::instance; }
    [[nodiscard]] const SpiBase& bus() const { return DeviceTrait<BusOrd>::instance; }
    [[nodiscard]] const SpiConfig& config() const { return m_config; }
    [[nodiscard]] constexpr uint8_t cs_index() const { return CsIndex; }

private:
    SpiConfig m_config {};
    SpiBase::ChipSelectFn m_chip_select {nullptr};
    void* m_chip_select_arg {nullptr};
    bool m_initialized {false};
};

namespace detail {

class SpiChipSelectGuard {
public:
    SpiChipSelectGuard(SpiBase::ChipSelectFn fn, void* arg)
        : fn_(fn), arg_(arg)
    {
        if (fn_ != nullptr) {
            fn_(arg_, true);
        }
    }

    ~SpiChipSelectGuard()
    {
        if (fn_ != nullptr) {
            fn_(arg_, false);
        }
    }

    SpiChipSelectGuard(const SpiChipSelectGuard&) = delete;
    SpiChipSelectGuard& operator=(const SpiChipSelectGuard&) = delete;

private:
    SpiBase::ChipSelectFn fn_;
    void* arg_;
};

} // namespace detail
} // namespace hal
