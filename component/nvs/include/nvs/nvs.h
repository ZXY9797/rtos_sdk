#pragma once

#include <nvs/config.h>
#include <nvs/types.h>

#include <drivers/status.h>
#include <osal/osal.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace nvs {

template <typename FlashT>
class Nvs {
public:
    explicit Nvs(FlashT& flash, uint32_t offset,
                 uint16_t sector_count = CONFIG_NVS_SECTOR_COUNT)
        : flash_(flash), offset_(offset),
          sector_size_(flash.erase_sector_size()), sector_count_(sector_count) {}

    ~Nvs() = default;
    Nvs(const Nvs&) = delete;
    Nvs& operator=(const Nvs&) = delete;

    [[nodiscard]] Status mount();
    [[nodiscard]] int32_t read(uint16_t id, void* data, size_t len);
    [[nodiscard]] int32_t write(uint16_t id, const void* data, size_t len);
    [[nodiscard]] Status remove(uint16_t id);
    [[nodiscard]] Status format();
    [[nodiscard]] int32_t available_space();
    [[nodiscard]] bool is_mounted() const {
        return mounted_.load(std::memory_order_acquire);
    }

    template <typename T>
    [[nodiscard]] int32_t read(uint16_t id, T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return read(id, &value, sizeof(T));
    }

    template <typename T>
    [[nodiscard]] int32_t write(uint16_t id, const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>);
        return write(id, &value, sizeof(T));
    }

private:
    struct __attribute__((packed)) SectorHeader {
        uint32_t magic;
        uint32_t sequence;
        uint16_t version;
        uint16_t header_size;
        uint32_t crc32;
    };

    static_assert(sizeof(SectorHeader) == 16U);

    static constexpr uint32_t kSectorMagic = 0x3253564EU; // "NVS2"
    static constexpr uint16_t kFormatVersion = 2U;
    static constexpr uint32_t kCommitErased = 0xFFFFFFFFU;
    static constexpr uint32_t kCommitDone = 0x00000000U;
    static constexpr uint8_t kAteCommitted = 0xA5U;
    static constexpr size_t kIoChunkSize = 32U;
    static constexpr uint32_t kMaxWriteBlockSize = 32U;

    FlashT& flash_;
    osal::Mutex lock_;
    uint32_t offset_;
    uint32_t sector_size_;
    uint16_t sector_count_;
    uint16_t cur_sector_ {0U};
    uint32_t sequence_ {0U};
    uint32_t data_wra_ {0U};
    uint32_t ate_wra_ {0U};
    std::atomic<bool> mounted_ {false};

    [[nodiscard]] uint32_t sector_base(uint16_t idx) const;
    [[nodiscard]] uint16_t next_sector(uint16_t idx) const;
    [[nodiscard]] static uint32_t align_up(uint32_t value, uint32_t alignment);
    [[nodiscard]] uint32_t data_start(uint16_t idx) const;
    [[nodiscard]] uint32_t header_commit_address(uint16_t idx) const;
    [[nodiscard]] uint32_t ate_descriptor_size() const;
    [[nodiscard]] uint32_t ate_slot_size() const;
    [[nodiscard]] uint32_t ate_commit_address(uint32_t addr) const;

    [[nodiscard]] hal::Status read_ate(uint32_t addr, Ate& ate) const;
    [[nodiscard]] hal::Status read_ate_commit(uint32_t addr,
                                              uint32_t& marker) const;
    [[nodiscard]] hal::Status read_commit_marker(uint32_t addr,
                                                 uint32_t& marker) const;
    [[nodiscard]] hal::Status write_ate_reservation(uint32_t addr, const Ate& ate);
    [[nodiscard]] hal::Status commit_ate(uint32_t addr, const Ate& ate);
    [[nodiscard]] static uint8_t compute_ate_crc(const Ate& ate);
    [[nodiscard]] static bool ate_valid(const Ate& ate);
    [[nodiscard]] static bool ate_erased(const Ate& ate);
    [[nodiscard]] Status read_header(uint16_t idx, SectorHeader& header) const;
    [[nodiscard]] Status open_sector(uint16_t idx, uint32_t sequence);
    [[nodiscard]] Status commit_sector(uint16_t idx);
    [[nodiscard]] Status scan_sector(uint16_t idx, uint32_t& ate_wra,
                                     uint32_t& data_wra) const;
    [[nodiscard]] Status find_latest(uint16_t id, Ate& found) const;
    [[nodiscard]] Status format_locked();
    [[nodiscard]] Status migrate_legacy_locked();
    [[nodiscard]] Status scan_legacy_sector(uint16_t idx,
                                            uint32_t& lowest_ate,
                                            uint16_t& entry_count,
                                            bool& closed) const;

    [[nodiscard]] Status append_entry(uint16_t id, const void* data, uint16_t len);
    [[nodiscard]] Status append_tombstone(uint16_t id);
    [[nodiscard]] Status copy_entry(uint16_t src_sector, const Ate& source);
    [[nodiscard]] Status gc_and_write(uint16_t id, const void* data, uint16_t len);
    [[nodiscard]] bool entry_matches(const Ate& entry, const void* data,
                                     size_t len) const;

    [[nodiscard]] static uint8_t crc8(const uint8_t* data, size_t len);
    [[nodiscard]] static uint32_t crc32_update(uint32_t crc,
                                               const uint8_t* data, size_t len);
    [[nodiscard]] static uint32_t crc32(const uint8_t* data, size_t len);
};

} // namespace nvs
