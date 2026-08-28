#pragma once

#include <device.h>
#include <drivers/dma.h>
#include <drivers/status.h>
#include <osal.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hal {

static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<uint8_t>::is_always_lock_free);

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
    using AsyncCallback = void (*)(void* arg, Status status);

    struct ChipSelect {
        ChipSelectFn function {nullptr};
        void* argument {nullptr};
    };

    [[nodiscard]] Status init(const SpiConfig& config);
    [[nodiscard]] Status deinit();
    [[nodiscard]] Status transfer(const uint8_t* tx, uint8_t* rx, size_t len,
                                  uint32_t timeout_ms,
                                  ChipSelect chip_select);
    [[nodiscard]] Status sync_send(const uint8_t* tx, uint8_t* rx, size_t len,
                                   uint32_t timeout_ms)
    {
        return transfer(tx, rx, len, timeout_ms, {});
    }

    /**
     * Reserve this controller for an ISR-triggered DMA stream.
     *
     * Call from task context before enabling the trigger interrupt. While the
     * reservation is held, synchronous transfers return Status::Busy.
     */
    [[nodiscard]] Status begin_async(AsyncCallback callback, void* argument);

    /**
     * Start one DMA transaction without waiting for completion.
     *
     * This method is ISR-safe after begin_async(). The supplied buffers and
     * chip-select context must remain valid until the callback runs in DMA ISR
     * context. A second transaction is rejected while one is active.
     */
    [[nodiscard]] Status transfer_async(
        const uint8_t* tx, uint8_t* rx, size_t len,
        ChipSelect chip_select);

    /**
     * Cancel any active transaction and release the async reservation.
     * Call only from task context after the trigger interrupt is detached.
     */
    [[nodiscard]] Status end_async();

    [[nodiscard]] bool async_busy() const
    {
        return m_async_active.load(std::memory_order_acquire);
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
    enum class DmaCompletion : uint8_t {
        Transmit = 0U,
        Receive,
    };

    explicit SpiBase(uintptr_t base) : m_base(base) {}

    void complete_async_from_isr(DmaCompletion completion);
    [[nodiscard]] Status configure_sync_dma(
        const uint8_t* tx, uint8_t* rx, size_t len,
        DmaChannel& tx_channel, DmaChannel& rx_channel);
    [[nodiscard]] Status wait_sync_dma(
        DmaChannel& tx_channel, DmaChannel& rx_channel,
        osal::Deadline& deadline);

    uintptr_t m_base;
    mutable osal::Mutex m_bus_mutex;
    osal::Semaphore m_xfer_sem {0U, 2U};
    SpiStats m_stats {};
    SpiConfig m_config {};
    DmaChannelConfig m_dma_tx {};
    DmaChannelConfig m_dma_rx {};
    std::atomic<uint8_t> m_dma_done_mask {0U};
    std::atomic<uint8_t> m_dma_error_mask {0U};
    DmaChannel m_async_tx_channel {0U, 0xFFU, 0xFFU};
    DmaChannel m_async_rx_channel {0U, 0xFFU, 0xFFU};
    std::atomic<bool> m_async_reserved {false};
    std::atomic<bool> m_async_active {false};
    AsyncCallback m_async_callback {nullptr};
    void* m_async_argument {nullptr};
    ChipSelectFn m_async_chip_select {nullptr};
    void* m_async_chip_select_arg {nullptr};
    uint8_t m_async_dummy_tx {0xFFU};
    uint8_t m_async_dummy_rx {0U};
    uint8_t m_sync_dummy_tx {0xFFU};
    uint8_t m_sync_dummy_rx {0U};
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
        if (config.mode != bus_config.mode
            || config.data_bits != bus_config.data_bits
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
        return bus().transfer(
            tx, rx, len, timeout_ms,
            {m_chip_select, m_chip_select_arg});
    }

    [[nodiscard]] Status begin_async(
        SpiBase::AsyncCallback callback, void* argument)
    {
        if (!m_initialized || m_chip_select == nullptr) {
            return Status::InvalidArgument;
        }
        return bus().begin_async(callback, argument);
    }

    [[nodiscard]] Status transfer_async(
        const uint8_t* tx, uint8_t* rx, size_t len)
    {
        if (!m_initialized || m_chip_select == nullptr) {
            return Status::InvalidArgument;
        }
        return bus().transfer_async(
            tx, rx, len, {m_chip_select, m_chip_select_arg});
    }

    [[nodiscard]] Status end_async()
    {
        return bus().end_async();
    }

    [[nodiscard]] bool is_initialized() const { return m_initialized; }
    [[nodiscard]] SpiBase& bus() { return DeviceTrait<BusOrd>::instance; }
    [[nodiscard]] const SpiBase& bus() const
    {
        return DeviceTrait<BusOrd>::instance;
    }
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
