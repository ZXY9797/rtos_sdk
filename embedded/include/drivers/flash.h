#pragma once

#include <drivers/status.h>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hal {

// Common Flash adapter for memory-mapped internal Flash and external NOR.
// Backends program complete, aligned write blocks. This class handles
// unaligned head/tail data with read-modify-write and restores the hardware
// lock before every return from a mutating operation.
class Flash {
public:
    using ReadFn        = Status(*)(void *ctx, uint32_t offset,
                                    void *data, size_t len);
    using UnlockFn      = void(*)(void *ctx);
    using WriteBlockFn  = Status(*)(void *ctx, uint32_t addr,
                                    const void *data, size_t len);
    using EraseSectorFn = Status(*)(void *ctx, uint32_t addr);
    using LockFn        = void(*)(void *ctx);

    Flash(uint32_t flash_base, uint32_t flash_size,
          uint32_t write_block_size,
          uint32_t sector_size, void *hw_ctx,
          WriteBlockFn write_block, EraseSectorFn erase_sector,
          LockFn lock_fn, UnlockFn unlock_fn = nullptr,
          ReadFn read_fn = nullptr)
        : m_flash_base(flash_base)
        , m_flash_size(flash_size)
        , m_write_block_size(write_block_size)
        , m_sector_size(sector_size)
        , m_hw_ctx(hw_ctx)
        , m_read_fn(read_fn)
        , m_write_block(write_block)
        , m_erase_sector(erase_sector)
        , m_lock(lock_fn)
        , m_unlock(unlock_fn) {}

    ~Flash() = default;

    Flash(const Flash &) = delete;
    Flash &operator=(const Flash &) = delete;

    Flash(Flash &&other) noexcept
        : m_flash_base(other.m_flash_base)
        , m_flash_size(other.m_flash_size)
        , m_write_block_size(other.m_write_block_size)
        , m_sector_size(other.m_sector_size)
        , m_hw_ctx(other.m_hw_ctx)
        , m_read_fn(other.m_read_fn)
        , m_write_block(other.m_write_block)
        , m_erase_sector(other.m_erase_sector)
        , m_lock(other.m_lock)
        , m_unlock(other.m_unlock)
        , m_initialized(other.m_initialized)
    {
        other.m_initialized = false;
    }

    Flash &operator=(Flash &&) = delete;

    [[nodiscard]] Status init() {
        const uint64_t address_space = static_cast<uint64_t>(UINT32_MAX) + 1U;
        if (m_flash_size == 0U
            || static_cast<uint64_t>(m_flash_base) + m_flash_size > address_space
            || m_write_block_size == 0
            || m_write_block_size > kMaxWriteBlockSize
            || m_sector_size == 0
            || m_write_block_size > m_sector_size
            || (m_sector_size % m_write_block_size) != 0U
            || (m_flash_size % m_sector_size) != 0U
            || m_write_block == nullptr
            || m_erase_sector == nullptr
            || m_lock == nullptr) {
            return Status::InvalidArgument;
        }
        m_initialized = true;
        return Status::Ok;
    }

    [[nodiscard]] Status deinit() {
        m_initialized = false;
        return Status::Ok;
    }

    [[nodiscard]] bool is_initialized() const {
        return m_initialized;
    }

    [[nodiscard]] uint32_t write_block_size() const {
        return m_write_block_size;
    }

    [[nodiscard]] uint32_t capacity() const {
        return m_flash_size;
    }

    [[nodiscard]] uint32_t erase_sector_size() const {
        return m_sector_size;
    }

    [[nodiscard]] Status read(uint32_t offset, void *data,
                              size_t length) const {
        if (!m_initialized || data == nullptr || length == 0
            || !range_valid(offset, length)) {
            return Status::InvalidArgument;
        }
        OperationGuard operation;
        if (!operation.owns_lock()) {
            return Status::Busy;
        }
        return read_unchecked(offset, data, length);
    }

    [[nodiscard]] Status write(uint32_t offset, const void *data,
                               size_t length) {
        if (!m_initialized || data == nullptr || length == 0
            || m_write_block_size == 0
            || m_write_block_size > kMaxWriteBlockSize
            || !range_valid(offset, length)) {
            return Status::InvalidArgument;
        }

        OperationGuard operation;
        if (!operation.owns_lock()) {
            return Status::Busy;
        }

        if (m_unlock != nullptr) m_unlock(m_hw_ctx);
        HardwareRelockGuard hardware_lock(*this);

        const auto *src = static_cast<const uint8_t *>(data);
        const uint32_t wbs = m_write_block_size;
        size_t done = 0;
        alignas(8) uint8_t block[kMaxWriteBlockSize];

        while (done < length) {
            const uint32_t current = offset + static_cast<uint32_t>(done);
            const uint32_t block_offset = current - (current % wbs);
            const size_t in_block = current - block_offset;
            const size_t available = wbs - in_block;
            const size_t chunk = (length - done < available)
                               ? length - done : available;
            const void *write_data = src + done;

            if (in_block != 0 || chunk != wbs) {
                Status status = read_unchecked(block_offset, block, wbs);
                if (status != Status::Ok) {
                    return status;
                }
                std::memcpy(block + in_block, src + done, chunk);
                write_data = block;
            }

            const uint64_t absolute = static_cast<uint64_t>(m_flash_base)
                                    + block_offset;
            if (absolute + wbs > static_cast<uint64_t>(UINT32_MAX) + 1U) {
                return Status::InvalidArgument;
            }
            Status status = m_write_block(m_hw_ctx,
                static_cast<uint32_t>(absolute), write_data, wbs);
            if (status != Status::Ok) {
                return status;
            }
            done += chunk;
        }

        return Status::Ok;
    }

    [[nodiscard]] Status erase(uint32_t offset, size_t length) {
        if (!m_initialized || m_sector_size == 0 || length == 0
            || (offset % m_sector_size) != 0
            || (length % m_sector_size) != 0
            || !range_valid(offset, length)) {
            return Status::InvalidArgument;
        }

        OperationGuard operation;
        if (!operation.owns_lock()) {
            return Status::Busy;
        }

        if (m_unlock != nullptr) m_unlock(m_hw_ctx);
        HardwareRelockGuard hardware_lock(*this);

        const uint32_t start_sector = offset / m_sector_size;
        const size_t num_sectors = length / m_sector_size;
        for (size_t sector = 0; sector < num_sectors; ++sector) {
            const uint64_t absolute = static_cast<uint64_t>(m_flash_base)
                + (static_cast<uint64_t>(start_sector) + sector)
                * m_sector_size;
            if (absolute > UINT32_MAX) {
                return Status::InvalidArgument;
            }
            Status status = m_erase_sector(m_hw_ctx,
                                           static_cast<uint32_t>(absolute));
            if (status != Status::Ok) {
                return status;
            }
        }

        return Status::Ok;
    }

private:
    static constexpr uint32_t kMaxWriteBlockSize = 32;

    class OperationGuard {
    public:
        OperationGuard()
            : owns_lock_(!s_operation_lock.test_and_set(
                  std::memory_order_acquire)) {}

        ~OperationGuard() {
            if (owns_lock_) {
                s_operation_lock.clear(std::memory_order_release);
            }
        }

        OperationGuard(const OperationGuard &) = delete;
        OperationGuard &operator=(const OperationGuard &) = delete;

        [[nodiscard]] bool owns_lock() const { return owns_lock_; }

    private:
        bool owns_lock_;
    };

    class HardwareRelockGuard {
    public:
        explicit HardwareRelockGuard(Flash &owner) : owner_(owner) {}
        ~HardwareRelockGuard() { owner_.m_lock(owner_.m_hw_ctx); }

        HardwareRelockGuard(const HardwareRelockGuard &) = delete;
        HardwareRelockGuard &operator=(const HardwareRelockGuard &) = delete;

    private:
        Flash &owner_;
    };

    [[nodiscard]] bool range_valid(uint32_t offset, size_t length) const {
        return offset < m_flash_size
            && length <= static_cast<uint64_t>(m_flash_size) - offset;
    }

    [[nodiscard]] Status read_unchecked(uint32_t offset, void *data,
                                        size_t length) const {
        if (m_read_fn != nullptr) {
            return m_read_fn(m_hw_ctx, offset, data, length);
        }
        const uint64_t absolute = static_cast<uint64_t>(m_flash_base) + offset;
        if (absolute > UINT32_MAX) return Status::InvalidArgument;
        const auto *src = reinterpret_cast<const uint8_t *>(
            static_cast<uintptr_t>(absolute));
        std::memcpy(data, src, length);
        return Status::Ok;
    }

    uint32_t m_flash_base;
    uint32_t m_flash_size;
    uint32_t m_write_block_size;
    uint32_t m_sector_size;
    void *m_hw_ctx;
    ReadFn m_read_fn;
    WriteBlockFn m_write_block;
    EraseSectorFn m_erase_sector;
    LockFn m_lock;
    UnlockFn m_unlock;
    bool m_initialized {false};

    inline static std::atomic_flag s_operation_lock = ATOMIC_FLAG_INIT;
};

// Provided by the enabled CONFIG_FLASH_* backend.
Flash flash_create_default();

} // namespace hal
