#include <drivers/i2c.h>

#include <assert.h>
#include <osal.h>

namespace hal {
namespace {

struct I2cRegs {
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t OAR1;
    volatile uint32_t OAR2;
    volatile uint32_t TIMINGR;
    volatile uint32_t TIMEOUTR;
    volatile uint32_t ISR;
    volatile uint32_t ICR;
    volatile uint32_t PECR;
    volatile uint32_t RXDR;
    volatile uint32_t TXDR;
};

constexpr uint32_t kCr1PeripheralEnable = 1U << 0U;
constexpr uint32_t kCr2Read = 1U << 10U;
constexpr uint32_t kCr2Start = 1U << 13U;
constexpr uint32_t kCr2Stop = 1U << 14U;
constexpr uint32_t kCr2NbytesPosition = 16U;
constexpr uint32_t kCr2AutoEnd = 1U << 25U;

constexpr uint32_t kIsrTxReady = 1U << 1U;
constexpr uint32_t kIsrRxReady = 1U << 2U;
constexpr uint32_t kIsrNack = 1U << 4U;
constexpr uint32_t kIsrStop = 1U << 5U;
constexpr uint32_t kIsrTransferComplete = 1U << 6U;
constexpr uint32_t kIsrBusError = 1U << 8U;
constexpr uint32_t kIsrBusy = 1U << 15U;

constexpr uint32_t kIcrNackClear = 1U << 4U;
constexpr uint32_t kIcrStopClear = 1U << 5U;
constexpr uint32_t kIcrBusErrorClear = 1U << 8U;
constexpr uint32_t kAllClearFlags =
    kIcrNackClear | kIcrStopClear | kIcrBusErrorClear;
constexpr size_t kMaxTransferBytes = 255U;

enum class PollResult : uint8_t {
    Ready,
    Nack,
    BusError,
    Timeout,
};

PollResult wait_for(I2cRegs &regs, uint32_t flags,
                    osal::Deadline &deadline) {
    for (;;) {
        const uint32_t status = regs.ISR;
        if ((status & kIsrNack) != 0U) return PollResult::Nack;
        if ((status & kIsrBusError) != 0U) return PollResult::BusError;
        if ((status & flags) != 0U) return PollResult::Ready;
        if (deadline.expired()) return PollResult::Timeout;
    }
}

PollResult wait_bus_idle(I2cRegs &regs, osal::Deadline &deadline) {
    while ((regs.ISR & kIsrBusy) != 0U) {
        if (deadline.expired()) return PollResult::Timeout;
    }
    return PollResult::Ready;
}

void abort_transfer(I2cRegs &regs) {
    regs.CR2 |= kCr2Stop;
    regs.ICR = kAllClearFlags;
}

Status record_result(PollResult result, I2cAtomicStats &stats,
                     bool data_phase = false) {
    switch (result) {
    case PollResult::Ready:
        return Status::Ok;
    case PollResult::Nack:
        if (data_phase) {
            stats.nack_data_count++;
        } else {
            stats.nack_addr_count++;
        }
        return Status::HardwareError;
    case PollResult::BusError:
        stats.bus_error_count++;
        return Status::HardwareError;
    case PollResult::Timeout:
    default:
        stats.timeout_count++;
        return Status::Timeout;
    }
}

uint32_t transfer_config(uint16_t address, size_t length,
                         bool read, bool auto_end) {
    return (static_cast<uint32_t>(address & 0x7FU) << 1U)
        | (static_cast<uint32_t>(length) << kCr2NbytesPosition)
        | (read ? kCr2Read : 0U)
        | (auto_end ? kCr2AutoEnd : 0U)
        | kCr2Start;
}

bool valid_transfer(uint16_t address, const void *data, size_t length) {
    return address <= 0x7FU && data != nullptr
        && length > 0U && length <= kMaxTransferBytes;
}

} // namespace

void I2cBase::isr_handler(osal::IsrContext &context) {
    (void)context;
    auto &regs = *reinterpret_cast<I2cRegs *>(m_base);
    const uint32_t status = regs.ISR;
    if ((status & kIsrNack) != 0U) m_stats.nack_addr_count++;
    if ((status & kIsrBusError) != 0U) m_stats.bus_error_count++;
    regs.ICR = kAllClearFlags;
}

Status I2cBase::init(uint32_t timing) {
    HAL_ASSERT(m_base != 0U);
    if (m_base == 0U || timing == 0U) return Status::InvalidArgument;
    if (!m_bus_mutex.is_valid()) return Status::NoMemory;

    auto &regs = *reinterpret_cast<I2cRegs *>(m_base);
    regs.CR1 = 0U;
    regs.TIMINGR = timing;
    regs.ICR = kAllClearFlags;
    regs.CR1 = kCr1PeripheralEnable;
    set_state(DeviceState::Initialized);
    return Status::Ok;
}

Status I2cBase::deinit() {
    if (!is_initialized()) return Status::Ok;
    auto &regs = *reinterpret_cast<I2cRegs *>(m_base);
    regs.CR1 = 0U;
    set_state(DeviceState::Created);
    return Status::Ok;
}

Status I2cBase::master_transmit(uint16_t addr, const uint8_t *data,
                               size_t len, uint32_t timeout_ms) {
    HAL_ASSERT_MSG(is_initialized(), "I2C not initialized");
    if (!is_initialized() || !valid_transfer(addr, data, len)) {
        return Status::InvalidArgument;
    }

    osal::Deadline deadline(timeout_ms);
    osal::LockGuard lock(m_bus_mutex, deadline.remaining());
    if (!lock.owns_lock()) return Status::Busy;
    auto &regs = *reinterpret_cast<I2cRegs *>(m_base);
    PollResult result = wait_bus_idle(regs, deadline);
    if (result != PollResult::Ready) return record_result(result, m_stats);

    regs.ICR = kAllClearFlags;
    regs.CR2 = transfer_config(addr, len, false, true);
    for (size_t index = 0U; index < len; ++index) {
        result = wait_for(regs, kIsrTxReady, deadline);
        if (result != PollResult::Ready) {
            abort_transfer(regs);
            return record_result(result, m_stats, index != 0U);
        }
        regs.TXDR = data[index];
    }
    result = wait_for(regs, kIsrStop, deadline);
    if (result != PollResult::Ready) {
        abort_transfer(regs);
        return record_result(result, m_stats, true);
    }
    regs.ICR = kIcrStopClear;
    m_stats.transfer_count++;
    return Status::Ok;
}

Status I2cBase::master_receive(uint16_t addr, uint8_t *data,
                              size_t len, uint32_t timeout_ms) {
    HAL_ASSERT_MSG(is_initialized(), "I2C not initialized");
    if (!is_initialized() || !valid_transfer(addr, data, len)) {
        return Status::InvalidArgument;
    }

    osal::Deadline deadline(timeout_ms);
    osal::LockGuard lock(m_bus_mutex, deadline.remaining());
    if (!lock.owns_lock()) return Status::Busy;
    auto &regs = *reinterpret_cast<I2cRegs *>(m_base);
    PollResult result = wait_bus_idle(regs, deadline);
    if (result != PollResult::Ready) return record_result(result, m_stats);

    regs.ICR = kAllClearFlags;
    regs.CR2 = transfer_config(addr, len, true, true);
    for (size_t index = 0U; index < len; ++index) {
        result = wait_for(regs, kIsrRxReady, deadline);
        if (result != PollResult::Ready) {
            abort_transfer(regs);
            return record_result(result, m_stats, index != 0U);
        }
        data[index] = static_cast<uint8_t>(regs.RXDR);
    }
    result = wait_for(regs, kIsrStop, deadline);
    if (result != PollResult::Ready) {
        abort_transfer(regs);
        return record_result(result, m_stats, true);
    }
    regs.ICR = kIcrStopClear;
    m_stats.transfer_count++;
    return Status::Ok;
}

Status I2cBase::mem_write(uint16_t addr, uint16_t mem_addr,
                          MemAddrSize addr_size, const uint8_t *data,
                          size_t len, uint32_t timeout_ms) {
    const size_t address_len = addr_size == MemAddrSize::Bit16 ? 2U : 1U;
    if (!is_initialized() || addr > 0x7FU || data == nullptr || len == 0U
        || address_len + len > kMaxTransferBytes) {
        return Status::InvalidArgument;
    }

    osal::Deadline deadline(timeout_ms);
    osal::LockGuard lock(m_bus_mutex, deadline.remaining());
    if (!lock.owns_lock()) return Status::Busy;
    auto &regs = *reinterpret_cast<I2cRegs *>(m_base);
    PollResult result = wait_bus_idle(regs, deadline);
    if (result != PollResult::Ready) return record_result(result, m_stats);

    regs.ICR = kAllClearFlags;
    regs.CR2 = transfer_config(addr, address_len + len, false, true);
    for (size_t index = 0U; index < address_len; ++index) {
        result = wait_for(regs, kIsrTxReady, deadline);
        if (result != PollResult::Ready) {
            abort_transfer(regs);
            return record_result(result, m_stats);
        }
        const uint32_t shift = static_cast<uint32_t>(
            (address_len - index - 1U) * 8U);
        regs.TXDR = static_cast<uint8_t>(mem_addr >> shift);
    }
    for (size_t index = 0U; index < len; ++index) {
        result = wait_for(regs, kIsrTxReady, deadline);
        if (result != PollResult::Ready) {
            abort_transfer(regs);
            return record_result(result, m_stats, true);
        }
        regs.TXDR = data[index];
    }
    result = wait_for(regs, kIsrStop, deadline);
    if (result != PollResult::Ready) {
        abort_transfer(regs);
        return record_result(result, m_stats, true);
    }
    regs.ICR = kIcrStopClear;
    m_stats.transfer_count++;
    return Status::Ok;
}

Status I2cBase::mem_read(uint16_t addr, uint16_t mem_addr,
                         MemAddrSize addr_size, uint8_t *data,
                         size_t len, uint32_t timeout_ms) {
    const size_t address_len = addr_size == MemAddrSize::Bit16 ? 2U : 1U;
    if (!is_initialized() || !valid_transfer(addr, data, len)) {
        return Status::InvalidArgument;
    }

    osal::Deadline deadline(timeout_ms);
    osal::LockGuard lock(m_bus_mutex, deadline.remaining());
    if (!lock.owns_lock()) return Status::Busy;
    auto &regs = *reinterpret_cast<I2cRegs *>(m_base);
    PollResult result = wait_bus_idle(regs, deadline);
    if (result != PollResult::Ready) return record_result(result, m_stats);

    regs.ICR = kAllClearFlags;
    regs.CR2 = transfer_config(addr, address_len, false, false);
    for (size_t index = 0U; index < address_len; ++index) {
        result = wait_for(regs, kIsrTxReady, deadline);
        if (result != PollResult::Ready) {
            abort_transfer(regs);
            return record_result(result, m_stats);
        }
        const uint32_t shift = static_cast<uint32_t>(
            (address_len - index - 1U) * 8U);
        regs.TXDR = static_cast<uint8_t>(mem_addr >> shift);
    }
    result = wait_for(regs, kIsrTransferComplete, deadline);
    if (result != PollResult::Ready) {
        abort_transfer(regs);
        return record_result(result, m_stats);
    }

    regs.CR2 = transfer_config(addr, len, true, true);
    for (size_t index = 0U; index < len; ++index) {
        result = wait_for(regs, kIsrRxReady, deadline);
        if (result != PollResult::Ready) {
            abort_transfer(regs);
            return record_result(result, m_stats, true);
        }
        data[index] = static_cast<uint8_t>(regs.RXDR);
    }
    result = wait_for(regs, kIsrStop, deadline);
    if (result != PollResult::Ready) {
        abort_transfer(regs);
        return record_result(result, m_stats, true);
    }
    regs.ICR = kIcrStopClear;
    m_stats.transfer_count++;
    return Status::Ok;
}

Status I2cBase::probe(uint16_t addr, uint32_t retries,
                      uint32_t timeout_ms) {
    if (!is_initialized() || addr > 0x7FU || retries == 0U) {
        return Status::InvalidArgument;
    }

    osal::Deadline deadline(timeout_ms);
    osal::LockGuard lock(m_bus_mutex, deadline.remaining());
    if (!lock.owns_lock()) return Status::Busy;
    auto &regs = *reinterpret_cast<I2cRegs *>(m_base);

    for (uint32_t attempt = 0U; attempt < retries; ++attempt) {
        PollResult result = wait_bus_idle(regs, deadline);
        if (result != PollResult::Ready) return record_result(result, m_stats);
        regs.ICR = kAllClearFlags;
        regs.CR2 = transfer_config(addr, 0U, false, true);
        result = wait_for(regs, kIsrStop, deadline);
        if (result == PollResult::Ready) {
            regs.ICR = kIcrStopClear;
            return Status::Ok;
        }
        regs.ICR = kAllClearFlags;
        if (result != PollResult::Nack) return record_result(result, m_stats);
        m_stats.nack_addr_count++;
    }
    return Status::HardwareError;
}

} // namespace hal
