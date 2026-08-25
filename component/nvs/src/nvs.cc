#include <nvs/nvs.h>

#include <drivers/flash.h>

#include <cstring>
#include <limits>

namespace nvs {

template <typename FlashT>
uint32_t Nvs<FlashT>::sector_base(uint16_t idx) const
{
    return offset_ + static_cast<uint32_t>(idx) * sector_size_;
}

template <typename FlashT>
uint16_t Nvs<FlashT>::next_sector(uint16_t idx) const
{
    return static_cast<uint16_t>((idx + 1U) % sector_count_);
}

template <typename FlashT>
uint32_t Nvs<FlashT>::align_up(uint32_t value, uint32_t alignment)
{
    if (alignment == 0U) {
        return value;
    }
    return ((value + alignment - 1U) / alignment) * alignment;
}

template <typename FlashT>
uint32_t Nvs<FlashT>::data_start(uint16_t idx) const
{
    return header_commit_address(idx) + flash_.write_block_size();
}

template <typename FlashT>
uint32_t Nvs<FlashT>::header_commit_address(uint16_t idx) const
{
    return sector_base(idx)
        + align_up(sizeof(SectorHeader), flash_.write_block_size());
}

template <typename FlashT>
uint32_t Nvs<FlashT>::ate_descriptor_size() const
{
    return align_up(sizeof(Ate), flash_.write_block_size());
}

template <typename FlashT>
uint32_t Nvs<FlashT>::ate_slot_size() const
{
    return ate_descriptor_size() + flash_.write_block_size();
}

template <typename FlashT>
uint32_t Nvs<FlashT>::ate_commit_address(uint32_t addr) const
{
    return addr + ate_descriptor_size();
}

template <typename FlashT>
Status Nvs<FlashT>::mount()
{
    if (osal::Kernel::in_isr()) {
        return Status::FlashError;
    }
    osal::LockGuard guard(lock_);
    if (!guard.owns_lock()) {
        return Status::FlashError;
    }
    if (mounted_) {
        return Status::Ok;
    }
    const uint32_t write_block_size = flash_.write_block_size();
    if (sector_count_ < 2U || sector_size_ == 0U
        || write_block_size < 4U
        || write_block_size > kMaxWriteBlockSize
        || (write_block_size & (write_block_size - 1U)) != 0U
        || (offset_ % sector_size_) != 0U
        || (offset_ % write_block_size) != 0U
        || sector_size_ <= (data_start(0U) - sector_base(0U)
                            + ate_slot_size())) {
        return Status::InvalidArgument;
    }

    bool found = false;
    bool saw_invalid = false;
    uint16_t newest_sector = 0U;
    uint32_t newest_sequence = 0U;
    for (uint16_t i = 0U; i < sector_count_; ++i) {
        SectorHeader header {};
        const Status status = read_header(i, header);
        if (status == Status::NotFound) {
            continue;
        }
        if (status != Status::Ok) {
            saw_invalid = true;
            continue;
        }
        uint32_t marker = kCommitErased;
        if (read_commit_marker(header_commit_address(i), marker)
                != hal::Status::Ok) {
            return Status::FlashError;
        }
        if (marker != kCommitDone) {
            continue;
        }
        if (!found || static_cast<int32_t>(header.sequence - newest_sequence) > 0) {
            found = true;
            newest_sector = i;
            newest_sequence = header.sequence;
        }
    }

    if (!found) {
        const Status migration_status = migrate_legacy_locked();
        if (migration_status == Status::Ok
            || migration_status == Status::MigrationRequired
            || migration_status == Status::FlashError
            || migration_status == Status::CrcError) {
            return migration_status;
        }
        return saw_invalid ? Status::CrcError : format_locked();
    }

    uint32_t ate_wra = 0U;
    uint32_t data_wra = 0U;
    const Status scan_status = scan_sector(newest_sector, ate_wra, data_wra);
    if (scan_status != Status::Ok) {
        return scan_status;
    }
    cur_sector_ = newest_sector;
    sequence_ = newest_sequence;
    ate_wra_ = ate_wra;
    data_wra_ = data_wra;
    mounted_ = true;
    return Status::Ok;
}

template <typename FlashT>
int32_t Nvs<FlashT>::read(uint16_t id, void* data, size_t len)
{
    if (osal::Kernel::in_isr()) {
        return -static_cast<int32_t>(Status::FlashError);
    }
    osal::LockGuard guard(lock_);
    if (!guard.owns_lock()) {
        return -static_cast<int32_t>(Status::FlashError);
    }
    if (!mounted_) {
        return -static_cast<int32_t>(Status::NotMounted);
    }
    if (id > kMaxId || data == nullptr) {
        return -static_cast<int32_t>(Status::InvalidArgument);
    }

    Ate entry {};
    const Status status = find_latest(id, entry);
    if (status != Status::Ok || entry.len == 0U) {
        return -static_cast<int32_t>(status == Status::Ok ? Status::NotFound : status);
    }
    if (entry.len > len) {
        return -static_cast<int32_t>(Status::InvalidArgument);
    }

    const uint32_t address = sector_base(cur_sector_) + entry.offset;
    if (flash_.read(address, data, entry.len) != hal::Status::Ok) {
        return -static_cast<int32_t>(Status::FlashError);
    }
#if CONFIG_NVS_DATA_CRC
    uint32_t stored_crc = 0U;
    if (flash_.read(address + entry.len, &stored_crc, sizeof(stored_crc))
        != hal::Status::Ok) {
        return -static_cast<int32_t>(Status::FlashError);
    }
    if (crc32(static_cast<const uint8_t*>(data), entry.len) != stored_crc) {
        return -static_cast<int32_t>(Status::CrcError);
    }
#endif
    return static_cast<int32_t>(entry.len);
}

template <typename FlashT>
int32_t Nvs<FlashT>::write(uint16_t id, const void* data, size_t len)
{
    if (osal::Kernel::in_isr()) {
        return -static_cast<int32_t>(Status::FlashError);
    }
    osal::LockGuard guard(lock_);
    if (!guard.owns_lock()) {
        return -static_cast<int32_t>(Status::FlashError);
    }
    if (!mounted_) {
        return -static_cast<int32_t>(Status::NotMounted);
    }
    if (id > kMaxId || len > std::numeric_limits<uint16_t>::max()
        || (len > 0U && data == nullptr)) {
        return -static_cast<int32_t>(Status::InvalidArgument);
    }
    if (len == 0U) {
        const Status remove_status = append_tombstone(id);
        const Status final_status = (remove_status == Status::NoSpace)
            ? gc_and_write(id, nullptr, 0U) : remove_status;
        return (final_status == Status::Ok) ? 0 : -static_cast<int32_t>(final_status);
    }

    Ate existing {};
    if (find_latest(id, existing) == Status::Ok && existing.len > 0U
        && entry_matches(existing, data, len)) {
        return 0;
    }

    Status status = append_entry(id, data, static_cast<uint16_t>(len));
    if (status == Status::NoSpace) {
        status = gc_and_write(id, data, static_cast<uint16_t>(len));
    }
    return (status == Status::Ok) ? static_cast<int32_t>(len)
                                  : -static_cast<int32_t>(status);
}

template <typename FlashT>
Status Nvs<FlashT>::remove(uint16_t id)
{
    if (osal::Kernel::in_isr()) {
        return Status::FlashError;
    }
    osal::LockGuard guard(lock_);
    if (!guard.owns_lock()) {
        return Status::FlashError;
    }
    if (!mounted_) {
        return Status::NotMounted;
    }
    if (id > kMaxId) {
        return Status::InvalidArgument;
    }
    Ate existing {};
    if (find_latest(id, existing) != Status::Ok || existing.len == 0U) {
        return Status::Ok;
    }
    Status status = append_tombstone(id);
    if (status == Status::NoSpace) {
        status = gc_and_write(id, nullptr, 0U);
    }
    return status;
}

template <typename FlashT>
Status Nvs<FlashT>::format()
{
    if (osal::Kernel::in_isr()) {
        return Status::FlashError;
    }
    osal::LockGuard guard(lock_);
    if (!guard.owns_lock()) {
        return Status::FlashError;
    }
    return format_locked();
}

template <typename FlashT>
Status Nvs<FlashT>::format_locked()
{
    mounted_ = false;
    for (uint16_t i = 0U; i < sector_count_; ++i) {
        if (flash_.erase(sector_base(i), sector_size_) != hal::Status::Ok) {
            return Status::FlashError;
        }
    }
    Status status = open_sector(0U, 1U);
    if (status == Status::Ok) {
        status = commit_sector(0U);
    }
    if (status != Status::Ok) {
        return status;
    }
    cur_sector_ = 0U;
    sequence_ = 1U;
    data_wra_ = data_start(0U);
    ate_wra_ = sector_base(0U) + sector_size_;
    mounted_ = true;
    return Status::Ok;
}

template <typename FlashT>
Status Nvs<FlashT>::scan_legacy_sector(uint16_t idx,
                                       uint32_t& lowest_ate,
                                       uint16_t& entry_count,
                                       bool& closed) const
{
    const uint32_t base = sector_base(idx);
    const uint32_t end = base + sector_size_;
    uint32_t cursor = end;
    lowest_ate = end;
    entry_count = 0U;
    closed = false;

    while (cursor >= base + kAteSize) {
        const uint32_t address = cursor - kAteSize;
        Ate ate {};
        if (read_ate(address, ate) != hal::Status::Ok) {
            return Status::FlashError;
        }
        if (ate_erased(ate)) {
            break;
        }
        const bool valid = ate.part == 0xFFU
            && ate.crc8 == compute_ate_crc(ate);
        if (!valid) {
            break;
        }
        if (ate.id == kCloseId && ate.len == 0U) {
            closed = true;
            break;
        }
        if (ate.id > kMaxId) {
            return Status::CrcError;
        }
        if (ate.len > 0U) {
            const uint32_t crc_size = CONFIG_NVS_DATA_CRC ? 4U : 0U;
            const uint32_t data_end = base + ate.offset
                + align_up(static_cast<uint32_t>(ate.len) + crc_size,
                           flash_.write_block_size());
            if (base + ate.offset < base || data_end > address) {
                return Status::CrcError;
            }
        }
        lowest_ate = address;
        ++entry_count;
        cursor = address;
    }
    return entry_count > 0U ? Status::Ok : Status::NotFound;
}

template <typename FlashT>
Status Nvs<FlashT>::migrate_legacy_locked()
{
    bool found = false;
    uint16_t source_sector = 0U;
    uint32_t source_lowest = 0U;

    for (uint16_t idx = 0U; idx < sector_count_; ++idx) {
        uint32_t lowest = 0U;
        uint16_t count = 0U;
        bool closed = false;
        const Status status = scan_legacy_sector(
            idx, lowest, count, closed);
        if (status == Status::FlashError || status == Status::CrcError) {
            return status;
        }
        if (status != Status::Ok || closed) {
            continue;
        }
        if (found) {
            return Status::MigrationRequired;
        }
        found = true;
        source_sector = idx;
        source_lowest = lowest;
    }

    if (!found) {
        return Status::NotFound;
    }

    const uint16_t target_sector = next_sector(source_sector);
    Status status = open_sector(target_sector, 1U);
    if (status != Status::Ok) {
        return status;
    }
    cur_sector_ = target_sector;
    sequence_ = 1U;
    data_wra_ = data_start(target_sector);
    ate_wra_ = sector_base(target_sector) + sector_size_;

    const uint32_t source_end = sector_base(source_sector) + sector_size_;
    for (uint32_t address = source_lowest; address < source_end;
         address += kAteSize) {
        Ate legacy {};
        if (read_ate(address, legacy) != hal::Status::Ok) {
            status = Status::FlashError;
            break;
        }
        const bool valid = legacy.part == 0xFFU
            && legacy.crc8 == compute_ate_crc(legacy);
        if (!valid || legacy.id == kCloseId) {
            continue;
        }
        Ate existing {};
        if (find_latest(legacy.id, existing) == Status::Ok) {
            continue;
        }
        status = legacy.len == 0U
            ? append_tombstone(legacy.id)
            : copy_entry(source_sector, legacy);
        if (status != Status::Ok) {
            break;
        }
    }

    if (status == Status::Ok) {
        status = commit_sector(target_sector);
    }
    if (status != Status::Ok) {
        mounted_ = false;
        return status;
    }
    mounted_ = true;
    return Status::Ok;
}

template <typename FlashT>
int32_t Nvs<FlashT>::available_space()
{
    if (osal::Kernel::in_isr()) {
        return -static_cast<int32_t>(Status::FlashError);
    }
    osal::LockGuard guard(lock_);
    if (!guard.owns_lock()) {
        return -static_cast<int32_t>(Status::FlashError);
    }
    if (!mounted_) {
        return -static_cast<int32_t>(Status::NotMounted);
    }
    if (ate_wra_ <= data_wra_ + ate_slot_size()) {
        return 0;
    }
    return static_cast<int32_t>(ate_wra_ - data_wra_ - ate_slot_size());
}

template <typename FlashT>
hal::Status Nvs<FlashT>::read_ate(uint32_t addr, Ate& ate) const
{
    return flash_.read(addr, &ate, sizeof(ate));
}

template <typename FlashT>
hal::Status Nvs<FlashT>::read_ate_commit(uint32_t addr,
                                         uint32_t& marker) const
{
    return read_commit_marker(ate_commit_address(addr), marker);
}

template <typename FlashT>
hal::Status Nvs<FlashT>::read_commit_marker(uint32_t addr,
                                            uint32_t& marker) const
{
    alignas(kMaxWriteBlockSize) uint8_t block[kMaxWriteBlockSize] {};
    const uint32_t size = flash_.write_block_size();
    const hal::Status status = flash_.read(addr, block, size);
    if (status != hal::Status::Ok) {
        return status;
    }
    bool all_erased = true;
    bool all_committed = true;
    for (uint32_t i = 0U; i < size; ++i) {
        all_erased = all_erased && block[i] == 0xFFU;
        all_committed = all_committed && block[i] == 0x00U;
    }
    marker = all_committed ? kCommitDone
        : (all_erased ? kCommitErased : 0x55555555U);
    return hal::Status::Ok;
}

template <typename FlashT>
hal::Status Nvs<FlashT>::write_ate_reservation(uint32_t addr, const Ate& ate)
{
    alignas(kMaxWriteBlockSize) uint8_t descriptor[kMaxWriteBlockSize] {};
    const uint32_t descriptor_size = ate_descriptor_size();
    if (descriptor_size > sizeof(descriptor)) {
        return hal::Status::InvalidArgument;
    }
    std::memset(descriptor, 0xFF, descriptor_size);
    Ate reservation = ate;
    reservation.part = kAteCommitted;
    reservation.crc8 = compute_ate_crc(reservation);
    std::memcpy(descriptor, &reservation, sizeof(reservation));
    return flash_.write(addr, descriptor, descriptor_size);
}

template <typename FlashT>
hal::Status Nvs<FlashT>::commit_ate(uint32_t addr, const Ate& ate)
{
    (void)ate;
    alignas(kMaxWriteBlockSize) uint8_t marker[kMaxWriteBlockSize] {};
    return flash_.write(ate_commit_address(addr), marker,
                        flash_.write_block_size());
}

template <typename FlashT>
uint8_t Nvs<FlashT>::compute_ate_crc(const Ate& ate)
{
    return crc8(reinterpret_cast<const uint8_t*>(&ate), sizeof(ate) - 1U);
}

template <typename FlashT>
bool Nvs<FlashT>::ate_valid(const Ate& ate)
{
    return ate.part == kAteCommitted && ate.crc8 == compute_ate_crc(ate);
}

template <typename FlashT>
bool Nvs<FlashT>::ate_erased(const Ate& ate)
{
    const uint8_t* const bytes = reinterpret_cast<const uint8_t*>(&ate);
    for (size_t i = 0U; i < sizeof(ate); ++i) {
        if (bytes[i] != 0xFFU) {
            return false;
        }
    }
    return true;
}

template <typename FlashT>
Status Nvs<FlashT>::read_header(uint16_t idx, SectorHeader& header) const
{
    if (flash_.read(sector_base(idx), &header, sizeof(header)) != hal::Status::Ok) {
        return Status::FlashError;
    }
    const uint8_t* const bytes = reinterpret_cast<const uint8_t*>(&header);
    bool erased = true;
    for (size_t i = 0U; i < sizeof(header); ++i) {
        if (bytes[i] != 0xFFU) {
            erased = false;
            break;
        }
    }
    if (erased) {
        return Status::NotFound;
    }
    if (header.magic != kSectorMagic || header.version != kFormatVersion
        || header.header_size != data_start(idx) - sector_base(idx)
        || header.crc32 != crc32(bytes, offsetof(SectorHeader, crc32))) {
        return Status::CrcError;
    }
    return Status::Ok;
}

template <typename FlashT>
Status Nvs<FlashT>::open_sector(uint16_t idx, uint32_t sequence)
{
    if (idx >= sector_count_) {
        return Status::InvalidArgument;
    }
    if (flash_.erase(sector_base(idx), sector_size_) != hal::Status::Ok) {
        return Status::FlashError;
    }
    SectorHeader header {};
    header.magic = kSectorMagic;
    header.sequence = sequence;
    header.version = kFormatVersion;
    header.header_size = static_cast<uint16_t>(
        data_start(idx) - sector_base(idx));
    header.crc32 = crc32(reinterpret_cast<const uint8_t*>(&header),
                         offsetof(SectorHeader, crc32));
    alignas(kMaxWriteBlockSize) uint8_t descriptor[kMaxWriteBlockSize] {};
    const uint32_t descriptor_size = align_up(
        sizeof(header), flash_.write_block_size());
    std::memset(descriptor, 0xFF, descriptor_size);
    std::memcpy(descriptor, &header, sizeof(header));
    return flash_.write(sector_base(idx), descriptor, descriptor_size)
            == hal::Status::Ok
        ? Status::Ok : Status::FlashError;
}

template <typename FlashT>
Status Nvs<FlashT>::commit_sector(uint16_t idx)
{
    alignas(kMaxWriteBlockSize) uint8_t marker[kMaxWriteBlockSize] {};
    return flash_.write(header_commit_address(idx), marker,
                        flash_.write_block_size()) == hal::Status::Ok
        ? Status::Ok : Status::FlashError;
}

template <typename FlashT>
Status Nvs<FlashT>::scan_sector(uint16_t idx, uint32_t& ate_wra,
                                uint32_t& data_wra) const
{
    const uint32_t base = sector_base(idx);
    const uint32_t minimum = data_start(idx);
    const uint32_t slot_size = ate_slot_size();
    uint32_t cursor = base + sector_size_;
    data_wra = minimum;
    while (cursor >= minimum + slot_size) {
        const uint32_t address = cursor - slot_size;
        Ate ate {};
        if (read_ate(address, ate) != hal::Status::Ok) {
            return Status::FlashError;
        }
        if (ate_erased(ate)) {
            break;
        }
        if (!ate_valid(ate)) {
            cursor = address;
            break;
        }
        uint32_t marker = kCommitErased;
        if (read_ate_commit(address, marker) != hal::Status::Ok) {
            return Status::FlashError;
        }
        if (marker != kCommitDone && marker != kCommitErased) {
            marker = kCommitErased;
        }
        if (ate.offset != 0xFFFFU && ate.len != 0xFFFFU && ate.len > 0U) {
            const uint32_t crc_size = CONFIG_NVS_DATA_CRC ? 4U : 0U;
            const uint32_t end = base + ate.offset
                + align_up(static_cast<uint32_t>(ate.len) + crc_size,
                           flash_.write_block_size());
            if (base + ate.offset < minimum || end > address) {
                return Status::CrcError;
            }
            if (end > data_wra) {
                data_wra = end;
            }
        }
        cursor = address;
    }
    ate_wra = cursor;
    return (data_wra <= ate_wra) ? Status::Ok : Status::CrcError;
}

template <typename FlashT>
Status Nvs<FlashT>::find_latest(uint16_t id, Ate& found) const
{
    const uint32_t end = sector_base(cur_sector_) + sector_size_;
    for (uint32_t address = ate_wra_; address < end;
         address += ate_slot_size()) {
        Ate ate {};
        if (read_ate(address, ate) != hal::Status::Ok) {
            return Status::FlashError;
        }
        uint32_t marker = kCommitErased;
        if (read_ate_commit(address, marker) != hal::Status::Ok) {
            return Status::FlashError;
        }
        if (ate_valid(ate) && marker == kCommitDone && ate.id == id) {
            found = ate;
            return Status::Ok;
        }
    }
    return Status::NotFound;
}

template <typename FlashT>
Status Nvs<FlashT>::append_entry(uint16_t id, const void* data, uint16_t len)
{
    if (data == nullptr || len == 0U) {
        return Status::InvalidArgument;
    }
    const uint32_t crc_size = CONFIG_NVS_DATA_CRC ? 4U : 0U;
    const uint32_t stored_len = static_cast<uint32_t>(len) + crc_size;
    const uint32_t padded_len = align_up(stored_len, flash_.write_block_size());
    const uint32_t new_data_wra = data_wra_ + padded_len;
    const uint32_t slot_size = ate_slot_size();
    if (ate_wra_ < slot_size) {
        return Status::NoSpace;
    }
    const uint32_t new_ate_wra = ate_wra_ - slot_size;
    if (new_data_wra > new_ate_wra
        || (data_wra_ - sector_base(cur_sector_)) > UINT16_MAX) {
        return Status::NoSpace;
    }

    Ate ate {};
    ate.id = id;
    ate.offset = static_cast<uint16_t>(data_wra_ - sector_base(cur_sector_));
    ate.len = len;
    ate.part = kAteCommitted;
    if (write_ate_reservation(new_ate_wra, ate) != hal::Status::Ok) {
        return Status::FlashError;
    }

    const uint32_t destination = data_wra_;
    data_wra_ = new_data_wra;
    ate_wra_ = new_ate_wra;
    const uint32_t value_crc = crc32(static_cast<const uint8_t*>(data), len);
    const uint8_t* const source = static_cast<const uint8_t*>(data);
    for (uint32_t offset = 0U; offset < padded_len; offset += kIoChunkSize) {
        alignas(4) uint8_t chunk[kIoChunkSize];
        std::memset(chunk, 0xFF, sizeof(chunk));
        const uint32_t chunk_len = ((padded_len - offset) < kIoChunkSize)
            ? (padded_len - offset) : static_cast<uint32_t>(kIoChunkSize);
        for (uint32_t i = 0U; i < chunk_len; ++i) {
            const uint32_t position = offset + i;
            if (position < len) {
                chunk[i] = source[position];
#if CONFIG_NVS_DATA_CRC
            } else if (position < static_cast<uint32_t>(len) + sizeof(value_crc)) {
                chunk[i] = reinterpret_cast<const uint8_t*>(&value_crc)[position - len];
#endif
            }
        }
        if (flash_.write(destination + offset, chunk, chunk_len) != hal::Status::Ok) {
            return Status::FlashError;
        }
    }
    return commit_ate(new_ate_wra, ate) == hal::Status::Ok
        ? Status::Ok : Status::FlashError;
}

template <typename FlashT>
Status Nvs<FlashT>::append_tombstone(uint16_t id)
{
    const uint32_t slot_size = ate_slot_size();
    if (ate_wra_ < data_wra_ + slot_size) {
        return Status::NoSpace;
    }
    const uint32_t address = ate_wra_ - slot_size;
    Ate ate {};
    ate.id = id;
    ate.offset = 0U;
    ate.len = 0U;
    ate.part = kAteCommitted;
    if (write_ate_reservation(address, ate) != hal::Status::Ok) {
        return Status::FlashError;
    }
    ate_wra_ = address;
    return commit_ate(address, ate) == hal::Status::Ok
        ? Status::Ok : Status::FlashError;
}

template <typename FlashT>
bool Nvs<FlashT>::entry_matches(const Ate& entry, const void* data, size_t len) const
{
    if (entry.len != len || data == nullptr) {
        return false;
    }
    const uint8_t* const expected = static_cast<const uint8_t*>(data);
    const uint32_t address = sector_base(cur_sector_) + entry.offset;
    uint8_t chunk[kIoChunkSize] {};
    for (size_t offset = 0U; offset < len; offset += sizeof(chunk)) {
        const size_t count = ((len - offset) < sizeof(chunk))
            ? (len - offset) : sizeof(chunk);
        if (flash_.read(address + static_cast<uint32_t>(offset), chunk, count)
            != hal::Status::Ok || std::memcmp(chunk, expected + offset, count) != 0) {
            return false;
        }
    }
#if CONFIG_NVS_DATA_CRC
    uint32_t stored_crc = 0U;
    return flash_.read(address + entry.len, &stored_crc, sizeof(stored_crc))
               == hal::Status::Ok
        && stored_crc == crc32(expected, len);
#else
    return true;
#endif
}

template <typename FlashT>
Status Nvs<FlashT>::copy_entry(uint16_t src_sector, const Ate& source)
{
    const uint32_t crc_size = CONFIG_NVS_DATA_CRC ? 4U : 0U;
    const uint32_t stored_len = static_cast<uint32_t>(source.len) + crc_size;
    const uint32_t padded_len = align_up(stored_len, flash_.write_block_size());
    const uint32_t new_data_wra = data_wra_ + padded_len;
    const uint32_t slot_size = ate_slot_size();
    if (ate_wra_ < slot_size) {
        return Status::NoSpace;
    }
    const uint32_t new_ate_wra = ate_wra_ - slot_size;
    if (new_data_wra > new_ate_wra) {
        return Status::NoSpace;
    }

    Ate destination_ate = source;
    destination_ate.offset = static_cast<uint16_t>(
        data_wra_ - sector_base(cur_sector_));
    if (write_ate_reservation(new_ate_wra, destination_ate) != hal::Status::Ok) {
        return Status::FlashError;
    }
    const uint32_t source_address = sector_base(src_sector) + source.offset;
    const uint32_t destination_address = data_wra_;
    data_wra_ = new_data_wra;
    ate_wra_ = new_ate_wra;
    uint32_t running_crc = 0xFFFFFFFFU;
    uint32_t stored_crc = 0U;
#if CONFIG_NVS_DATA_CRC
    if (flash_.read(source_address + source.len, &stored_crc, sizeof(stored_crc))
        != hal::Status::Ok) {
        return Status::FlashError;
    }
#endif
    for (uint32_t offset = 0U; offset < padded_len; offset += kIoChunkSize) {
        alignas(4) uint8_t chunk[kIoChunkSize] {};
        const uint32_t count = ((padded_len - offset) < kIoChunkSize)
            ? (padded_len - offset) : static_cast<uint32_t>(kIoChunkSize);
        if (flash_.read(source_address + offset, chunk, count) != hal::Status::Ok
            || flash_.write(destination_address + offset, chunk, count)
               != hal::Status::Ok) {
            return Status::FlashError;
        }
        const uint32_t payload_count = (offset >= source.len) ? 0U
            : (((source.len - offset) < count) ? (source.len - offset) : count);
        running_crc = crc32_update(running_crc, chunk, payload_count);
    }
#if CONFIG_NVS_DATA_CRC
    if ((running_crc ^ 0xFFFFFFFFU) != stored_crc) {
        return Status::CrcError;
    }
#endif
    return commit_ate(new_ate_wra, destination_ate) == hal::Status::Ok
        ? Status::Ok : Status::FlashError;
}

template <typename FlashT>
Status Nvs<FlashT>::gc_and_write(uint16_t id, const void* data, uint16_t len)
{
    const uint16_t old_sector = cur_sector_;
    const uint32_t old_ate_wra = ate_wra_;
    const uint32_t old_data_wra = data_wra_;
    const uint16_t target_sector = next_sector(old_sector);
    const uint32_t target_sequence = sequence_ + 1U;

    Status status = open_sector(target_sector, target_sequence);
    if (status != Status::Ok) {
        return status;
    }
    cur_sector_ = target_sector;
    ate_wra_ = sector_base(target_sector) + sector_size_;
    data_wra_ = data_start(target_sector);

    const uint32_t old_end = sector_base(old_sector) + sector_size_;
    const uint32_t slot_size = ate_slot_size();
    for (uint32_t address = old_ate_wra; address < old_end;
         address += slot_size) {
        Ate candidate {};
        if (read_ate(address, candidate) != hal::Status::Ok) {
            status = Status::FlashError;
            break;
        }
        uint32_t candidate_marker = kCommitErased;
        if (read_ate_commit(address, candidate_marker) != hal::Status::Ok) {
            status = Status::FlashError;
            break;
        }
        if (!ate_valid(candidate) || candidate_marker != kCommitDone
            || candidate.id == id || candidate.len == 0U) {
            continue;
        }
        bool superseded = false;
        for (uint32_t newer = old_ate_wra; newer < address;
             newer += slot_size) {
            Ate newer_ate {};
            if (read_ate(newer, newer_ate) != hal::Status::Ok) {
                status = Status::FlashError;
                superseded = true;
                break;
            }
            uint32_t newer_marker = kCommitErased;
            if (read_ate_commit(newer, newer_marker) != hal::Status::Ok) {
                status = Status::FlashError;
                superseded = true;
                break;
            }
            if (ate_valid(newer_ate) && newer_marker == kCommitDone
                && newer_ate.id == candidate.id) {
                superseded = true;
                break;
            }
        }
        if (status != Status::Ok) {
            break;
        }
        if (!superseded) {
            status = copy_entry(old_sector, candidate);
            if (status != Status::Ok) {
                break;
            }
        }
    }

    if (status == Status::Ok) {
        status = (len > 0U) ? append_entry(id, data, len)
                            : append_tombstone(id);
    }
    if (status == Status::Ok) {
        status = commit_sector(target_sector);
    }
    if (status == Status::Ok) {
        sequence_ = target_sequence;
        return Status::Ok;
    }

    cur_sector_ = old_sector;
    ate_wra_ = old_ate_wra;
    data_wra_ = old_data_wra;
    return status;
}

template <typename FlashT>
uint8_t Nvs<FlashT>::crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0xFFU;
    for (size_t i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x80U) != 0U
                ? static_cast<uint8_t>((crc << 1U) ^ 0x31U)
                : static_cast<uint8_t>(crc << 1U);
        }
    }
    return crc;
}

template <typename FlashT>
uint32_t Nvs<FlashT>::crc32_update(uint32_t crc, const uint8_t* data, size_t len)
{
    for (size_t i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return crc;
}

template <typename FlashT>
uint32_t Nvs<FlashT>::crc32(const uint8_t* data, size_t len)
{
    return crc32_update(0xFFFFFFFFU, data, len) ^ 0xFFFFFFFFU;
}

template class Nvs<hal::Flash>;

} // namespace nvs
