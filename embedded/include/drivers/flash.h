#pragma once

#include <drivers/status.h>
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace hal {

// 通用 Flash 驱动 — 通过函数指针适配片内 Flash 和 NOR Flash
//
// 片内 Flash：read 走 memory-mapped memcpy，read_fn 留空
// NOR Flash：read 走 SPI/QSPI 回调，传入 read_fn
//
// 新增 Flash 只需实现回调 + 一个工厂函数：
//   read_data(ctx, offset, data, len) — 读取（NOR 必须，片内可选）
//   write_block(ctx, addr, data, len) — 写一个对齐块
//   erase_sector(ctx, addr)           — 擦一个扇区
//   lock(ctx)                         — 锁定 Flash
//   unlock(ctx)                       — 解锁 Flash（可选，默认空）
class Flash {
public:
    using ReadFn        = Status(*)(void *ctx, uint32_t offset,
                                    void *data, size_t len);
    using UnlockFn      = void(*)(void *ctx);
    using WriteBlockFn  = Status(*)(void *ctx, uint32_t addr,
                                    const void *data, size_t len);
    using EraseSectorFn = Status(*)(void *ctx, uint32_t addr);
    using LockFn        = void(*)(void *ctx);

    Flash(uint32_t flash_base, uint32_t write_block_size,
             uint32_t sector_size, void *hw_ctx,
             WriteBlockFn write_block, EraseSectorFn erase_sector,
             LockFn lock_fn, UnlockFn unlock_fn = nullptr,
             ReadFn read_fn = nullptr)
        : m_flash_base(flash_base)
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
        , m_write_block_size(other.m_write_block_size)
        , m_sector_size(other.m_sector_size)
        , m_hw_ctx(other.m_hw_ctx)
        , m_read_fn(other.m_read_fn)
        , m_write_block(other.m_write_block)
        , m_erase_sector(other.m_erase_sector)
        , m_lock(other.m_lock)
        , m_unlock(other.m_unlock)
        , m_initialized(other.m_initialized) {}

    Flash &operator=(Flash &&) = delete;

    [[nodiscard]] Status init() {
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
    [[nodiscard]] uint32_t erase_sector_size() const {
        return m_sector_size;
    }

    [[nodiscard]] Status read(uint32_t offset, void *data,
                              size_t length) const {
        if (!data || length == 0) return Status::InvalidArgument;
        if (m_read_fn) {
            return m_read_fn(m_hw_ctx, offset, data, length);
        }
        const auto *src = reinterpret_cast<const uint8_t *>(
            m_flash_base + offset);
        std::memcpy(data, src, length);
        return Status::Ok;
    }

    [[nodiscard]] Status write(uint32_t offset, const void *data,
                               size_t length) {
        if (!m_initialized || !data || length == 0) {
            return Status::InvalidArgument;
        }

        if (m_unlock) m_unlock(m_hw_ctx);

        uint32_t addr = m_flash_base + offset;
        const auto *src = static_cast<const uint8_t *>(data);
        const uint32_t wbs = m_write_block_size;

        size_t i = 0;
        while (i + (wbs - 1) < length) {
            Status s = m_write_block(m_hw_ctx, addr,
                                     &src[i], wbs);
            if (s != Status::Ok) {
                m_lock(m_hw_ctx);
                return s;
            }
            addr += wbs;
            i += wbs;
        }

        if (i < length) {
            alignas(4) uint8_t buf[8] = {
                0xFF, 0xFF, 0xFF, 0xFF,
                0xFF, 0xFF, 0xFF, 0xFF
            };
            std::memcpy(buf, &src[i], length - i);
            Status s = m_write_block(m_hw_ctx, addr, buf, wbs);
            if (s != Status::Ok) {
                m_lock(m_hw_ctx);
                return s;
            }
        }

        m_lock(m_hw_ctx);
        return Status::Ok;
    }

    [[nodiscard]] Status erase(uint32_t offset, size_t length) {
        if (!m_initialized || m_sector_size == 0 || length == 0) {
            return Status::InvalidArgument;
        }

        if (m_unlock) m_unlock(m_hw_ctx);

        uint32_t start_sector = offset / m_sector_size;
        uint32_t num_sectors = ((offset % m_sector_size) + length +
                                m_sector_size - 1) / m_sector_size;

        for (uint32_t s = 0; s < num_sectors; s++) {
            uint32_t addr = m_flash_base
                            + (start_sector + s) * m_sector_size;
            Status ret = m_erase_sector(m_hw_ctx, addr);
            if (ret != Status::Ok) {
                m_lock(m_hw_ctx);
                return ret;
            }
        }

        m_lock(m_hw_ctx);
        return Status::Ok;
    }

private:
    uint32_t m_flash_base;
    uint32_t m_write_block_size;
    uint32_t m_sector_size;
    void *m_hw_ctx;
    ReadFn m_read_fn;
    WriteBlockFn m_write_block;
    EraseSectorFn m_erase_sector;
    LockFn m_lock;
    UnlockFn m_unlock;
    bool m_initialized {false};
};

// Provided by the enabled CONFIG_FLASH_* backend.
Flash flash_create_default();

} // namespace hal
