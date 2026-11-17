#pragma once

#include <drivers/status.h>
#include <cstddef>
#include <cstdint>

namespace hal {

enum class DmaAccess : uint8_t { DeviceReads, DeviceWrites };

// Pure validation shared with host tests. Cache-managed buffers own whole
// lines: invalidating adjacent CPU-owned bytes is deliberately forbidden.
struct DmaRegion {
    uintptr_t begin {0U}, end {0U};
    size_t cache_line {1U};
    bool accepts(const void* pointer, size_t size) const {
        const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
        return pointer != nullptr && size > 0U && begin < end && address >= begin
            && address < end && size <= end - address && cache_line > 0U
            && (cache_line & (cache_line - 1U)) == 0U
            && address % cache_line == 0U && size % cache_line == 0U;
    }
};

// Provider is bounded and ISR-safe. Cached SoCs must supply a product policy
// covering DMA reachability and clean/invalidate; no weak no-op fallback.
[[nodiscard]] Status dma_prepare(const void* buffer, size_t size, DmaAccess access);
[[nodiscard]] Status dma_complete(const void* buffer, size_t size, DmaAccess access);

// Owns both buffers between prepare and finish. The driver must stop DMA before
// finish/destruction; completion runs for partial/error transfers too.
class DmaBufferLease {
public:
    DmaBufferLease() = default;

    ~DmaBufferLease()
    {
        (void)finish();
    }

    DmaBufferLease(const DmaBufferLease&) = delete;
    DmaBufferLease& operator=(const DmaBufferLease&) = delete;

    [[nodiscard]] Status prepare(const void* tx, size_t tx_size, void* rx, size_t rx_size)
    {
        if (tx_ != nullptr || rx_ != nullptr) {
            return Status::Busy;
        }
        if (tx == nullptr || rx == nullptr || tx_size == 0U || rx_size == 0U) {
            return Status::InvalidArgument;
        }

        Status status = dma_prepare(tx, tx_size, DmaAccess::DeviceReads);
        if (status != Status::Ok) {
            return status;
        }
        tx_ = tx;
        tx_size_ = tx_size;
        status = dma_prepare(rx, rx_size, DmaAccess::DeviceWrites);
        if (status != Status::Ok) {
            (void)finish();
            return status;
        }
        rx_ = rx;
        rx_size_ = rx_size;
        return Status::Ok;
    }

    [[nodiscard]] Status finish()
    {
        Status status = Status::Ok;
        if (rx_ != nullptr) {
            status = dma_complete(rx_, rx_size_, DmaAccess::DeviceWrites);
        }
        if (tx_ != nullptr) {
            const Status tx_status = dma_complete(tx_, tx_size_, DmaAccess::DeviceReads);
            if (status == Status::Ok) {
                status = tx_status;
            }
        }
        rx_ = tx_ = nullptr;
        return status;
    }

private:
    const void* tx_ {nullptr};
    const void* rx_ {nullptr};
    size_t tx_size_ {0U}, rx_size_ {0U};
};

} // namespace hal
