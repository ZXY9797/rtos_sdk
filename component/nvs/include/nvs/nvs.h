#pragma once

#include <nvs/types.h>
#include <nvs/config.h>
#include <drivers/status.h>
#include <osal/osal.h>
#include <cstdint>
#include <cstddef>
#include <type_traits>
#include <cstring>

namespace nvs {

template <typename FlashT>
class Nvs {
public:
    explicit Nvs(FlashT &flash, uint32_t offset,
                 uint16_t sector_count = CONFIG_NVS_SECTOR_COUNT)
        : flash_(flash)
        , offset_(offset)
        , sector_size_(flash.erase_sector_size())
        , sector_count_(sector_count) {}

    ~Nvs() = default;

    Nvs(const Nvs &) = delete;
    Nvs &operator=(const Nvs &) = delete;

    [[nodiscard]] Status mount();
    [[nodiscard]] int32_t read(uint16_t id, void *data, size_t len);
    [[nodiscard]] int32_t write(uint16_t id, const void *data,
                                size_t len);
    [[nodiscard]] Status remove(uint16_t id);
    [[nodiscard]] Status format();
    [[nodiscard]] int32_t available_space();
    [[nodiscard]] bool is_mounted() const { return mounted_; }

    template <typename T>
    [[nodiscard]] int32_t read(uint16_t id, T &value) {
        static_assert(std::is_trivially_copyable_v<T>, "");
        return read(id, &value, sizeof(T));
    }

    template <typename T>
    [[nodiscard]] int32_t write(uint16_t id, const T &value) {
        static_assert(std::is_trivially_copyable_v<T>, "");
        return write(id, &value, sizeof(T));
    }

private:
    FlashT &flash_;
    osal::Mutex lock_;
    uint32_t offset_;
    uint32_t sector_size_;
    uint16_t sector_count_;
    uint16_t cur_sector_ {0};
    uint32_t data_wra_ {0};
    uint32_t ate_wra_ {0};
    bool mounted_ {false};

    // 地址计算
    uint32_t sector_base(uint16_t idx) const {
        return offset_ + static_cast<uint32_t>(idx) * sector_size_;
    }
    uint16_t next_sector(uint16_t idx) const {
        return static_cast<uint16_t>((idx + 1) % sector_count_);
    }
    uint32_t align_up(uint32_t v, uint32_t a) const {
        return (v + a - 1) & ~(a - 1);
    }

    // ATE 操作
    hal::Status read_ate(uint32_t addr, Ate &ate) const;
    hal::Status write_ate(uint32_t addr, const Ate &ate);
    static uint8_t compute_ate_crc(const Ate &ate);
    static bool ate_valid(const Ate &ate);
    static bool is_close_ate(const Ate &ate);

    // 扫描与恢复
    Status scan_sector(uint16_t idx, uint32_t &ate_end,
                       uint32_t &data_end, bool &closed);
    Status find_latest(uint16_t id, Ate &found, uint16_t &sector);
    Status recover_gc();

    // 写入
    Status append_entry(uint16_t id, const void *data, uint16_t len);
    Status gc_and_write(uint16_t id, const void *data, uint16_t len);
    Status close_sector(uint16_t idx);
    Status open_sector(uint16_t idx);

    // CRC
    static uint8_t crc8(const uint8_t *data, size_t len);
    static uint32_t crc32(const uint8_t *data, size_t len);
};

} // namespace nvs
