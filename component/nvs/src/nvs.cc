#include <nvs/nvs.h>
#include <drivers/flash.h>

namespace nvs {

// ==================== 公有方法 ====================

template <typename FlashT>
Status Nvs<FlashT>::mount() {
    if (osal::Kernel::in_isr()) return Status::FlashError;
    osal::LockGuard guard(lock_);

    uint16_t open_count = 0;
    uint16_t open_idx = 0;
    uint16_t closed_count = 0;

    for (uint16_t i = 0; i < sector_count_; i++) {
        uint32_t ate_end = 0;
        uint32_t data_end = 0;
        bool closed = false;
        scan_sector(i, ate_end, data_end, closed);

        if (closed) {
            closed_count++;
        } else {
            open_count++;
            open_idx = i;
        }
    }

    if (open_count == 0 && closed_count == 0) {
        return format();
    }

    if (open_count == 0) {
        return format();
    }

    if (open_count > 1) {
        return recover_gc();
    }

    cur_sector_ = open_idx;

    uint32_t ate_end = 0;
    uint32_t data_end = 0;
    bool closed = false;
    scan_sector(cur_sector_, ate_end, data_end, closed);

    ate_wra_ = ate_end;
    data_wra_ = data_end;
    mounted_ = true;

    return Status::Ok;
}

template <typename FlashT>
int32_t Nvs<FlashT>::read(uint16_t id, void *data, size_t len) {
    if (osal::Kernel::in_isr()) return -1;
    osal::LockGuard guard(lock_);
    if (!mounted_) return -static_cast<int32_t>(Status::NotMounted);
    if (id > kMaxId) return -static_cast<int32_t>(Status::InvalidArgument);

    Ate found{};
    uint16_t sector = 0;
    Status s = find_latest(id, found, sector);
    if (s != Status::Ok) return -static_cast<int32_t>(s);

    if (found.len == 0) {
        return -static_cast<int32_t>(Status::NotFound);
    }
    if (found.len > len) {
        return -static_cast<int32_t>(Status::InvalidArgument);
    }

    uint32_t addr = sector_base(sector) + found.offset;
    hal::Status hs = flash_.read(addr, data, found.len);
    if (hs != hal::Status::Ok) {
        return -static_cast<int32_t>(Status::FlashError);
    }

#if CONFIG_NVS_DATA_CRC
    if (found.len > 4) {
        uint32_t stored_crc;
        uint32_t crc_addr = sector_base(sector) + found.offset
                            + found.len - 4;
        if (flash_.read(crc_addr, &stored_crc, 4) == hal::Status::Ok) {
            uint32_t calc = crc32(static_cast<const uint8_t *>(data),
                                  found.len - 4);
            if (calc != stored_crc) {
                return -static_cast<int32_t>(Status::CrcError);
            }
        }
    }
    return static_cast<int32_t>(found.len) - 4;
#else
    return static_cast<int32_t>(found.len);
#endif
}

template <typename FlashT>
int32_t Nvs<FlashT>::write(uint16_t id, const void *data, size_t len) {
    if (osal::Kernel::in_isr()) return -1;
    osal::LockGuard guard(lock_);
    if (!mounted_) return -static_cast<int32_t>(Status::NotMounted);
    if (id > kMaxId) return -static_cast<int32_t>(Status::InvalidArgument);

    if (len == 0 || data == nullptr) {
        Status s = remove(id);
        return (s == Status::Ok) ? 0 : -static_cast<int32_t>(s);
    }

    Ate existing{};
    uint16_t exist_sector = 0;
    if (find_latest(id, existing, exist_sector) == Status::Ok
        && existing.len > 0) {
        uint8_t buf[256];
        if (existing.len <= sizeof(buf)) {
            uint32_t addr = sector_base(exist_sector) + existing.offset;
            if (flash_.read(addr, buf, existing.len) == hal::Status::Ok
                && std::memcmp(buf, data, len) == 0) {
                return 0;
            }
        }
    }

    Status s = append_entry(id, data, static_cast<uint16_t>(len));
    if (s == Status::NoSpace) {
        s = gc_and_write(id, data, static_cast<uint16_t>(len));
    }
    if (s != Status::Ok) return -static_cast<int32_t>(s);

    return static_cast<int32_t>(len);
}

template <typename FlashT>
Status Nvs<FlashT>::remove(uint16_t id) {
    if (osal::Kernel::in_isr()) return Status::FlashError;
    osal::LockGuard guard(lock_);
    if (!mounted_) return Status::NotMounted;

    Ate ate{};
    ate.id = id;
    ate.offset = 0;
    ate.len = 0;
    ate.part = 0xFF;

    uint32_t next_ate = ate_wra_ - static_cast<uint32_t>(kAteSize);
    if (next_ate < data_wra_) {
        return gc_and_write(id, nullptr, 0);
    }

    hal::Status hs = write_ate(next_ate, ate);
    if (hs != hal::Status::Ok) return Status::FlashError;

    ate_wra_ = next_ate;
    return Status::Ok;
}

template <typename FlashT>
Status Nvs<FlashT>::format() {
    for (uint16_t i = 0; i < sector_count_; i++) {
        hal::Status hs = flash_.erase(sector_base(i), sector_size_);
        if (hs != hal::Status::Ok) return Status::FlashError;
    }

    cur_sector_ = 0;
    data_wra_ = sector_base(0);
    ate_wra_ = sector_base(0) + sector_size_;
    mounted_ = true;

    return Status::Ok;
}

template <typename FlashT>
int32_t Nvs<FlashT>::available_space() {
    osal::LockGuard guard(lock_);
    if (!mounted_) return -static_cast<int32_t>(Status::NotMounted);
    if (ate_wra_ <= data_wra_) return 0;
    return static_cast<int32_t>(ate_wra_ - data_wra_);
}

// ==================== ATE 操作 ====================

template <typename FlashT>
hal::Status Nvs<FlashT>::read_ate(uint32_t addr, Ate &ate) const {
    return flash_.read(addr, &ate, kAteSize);
}

template <typename FlashT>
hal::Status Nvs<FlashT>::write_ate(uint32_t addr, const Ate &ate) {
    Ate copy = ate;
    copy.crc8 = compute_ate_crc(copy);
    return flash_.write(addr, &copy, kAteSize);
}

template <typename FlashT>
uint8_t Nvs<FlashT>::compute_ate_crc(const Ate &ate) {
    return crc8(reinterpret_cast<const uint8_t *>(&ate), kAteSize - 1);
}

template <typename FlashT>
bool Nvs<FlashT>::ate_valid(const Ate &ate) {
    return ate.crc8 == compute_ate_crc(ate);
}

template <typename FlashT>
bool Nvs<FlashT>::is_close_ate(const Ate &ate) {
    return ate.id == kCloseId && ate.len == 0;
}

// ==================== 扫描与恢复 ====================

template <typename FlashT>
Status Nvs<FlashT>::scan_sector(uint16_t idx, uint32_t &ate_end,
                                uint32_t &data_end, bool &closed) {
    closed = false;
    uint32_t base = sector_base(idx);
    ate_end = base;
    data_end = base;

    uint32_t max_ate = sector_size_ / kAteSize;
    for (uint32_t i = 0; i < max_ate; i++) {
        uint32_t addr = base + i * kAteSize;

        uint8_t raw[kAteSize];
        if (flash_.read(addr, raw, kAteSize) != hal::Status::Ok) break;
        bool all_ff = true;
        for (size_t b = 0; b < kAteSize; b++) {
            if (raw[b] != 0xFF) { all_ff = false; break; }
        }
        if (all_ff) break;

        Ate ate{};
        if (read_ate(addr, ate) != hal::Status::Ok) break;
        if (!ate_valid(ate)) break;

        if (is_close_ate(ate)) {
            closed = true;
            break;
        }

        ate_end = addr + static_cast<uint32_t>(kAteSize);

        if (ate.len > 0) {
            uint32_t end = base + ate.offset
                           + align_up(ate.len, flash_.write_block_size());
            if (end > data_end) data_end = end;
        }
    }

    return Status::Ok;
}

template <typename FlashT>
Status Nvs<FlashT>::find_latest(uint16_t id, Ate &found,
                                uint16_t &sector) {
    for (uint16_t s = 0; s < sector_count_; s++) {
        uint16_t idx = static_cast<uint16_t>(
            (cur_sector_ + sector_count_ - s) % sector_count_);
        uint32_t base = sector_base(idx);
        uint32_t max_ate = sector_size_ / kAteSize;

        for (uint32_t i = max_ate; i > 0; i--) {
            uint32_t addr = base + (i - 1) * kAteSize;
            Ate ate{};
            if (read_ate(addr, ate) != hal::Status::Ok) break;
            if (!ate_valid(ate)) break;
            if (is_close_ate(ate)) break;

            if (ate.id == id) {
                found = ate;
                sector = idx;
                return Status::Ok;
            }
        }
    }
    return Status::NotFound;
}

template <typename FlashT>
Status Nvs<FlashT>::recover_gc() {
    uint16_t opens[2];
    uint8_t cnt = 0;
    for (uint16_t i = 0; i < sector_count_ && cnt < 2; i++) {
        uint32_t ate_end = 0;
        uint32_t data_end = 0;
        bool closed = false;
        scan_sector(i, ate_end, data_end, closed);
        if (!closed) {
            opens[cnt++] = i;
        }
    }
    if (cnt < 2) return Status::FlashError;

    hal::Status hs = flash_.erase(sector_base(opens[0]), sector_size_);
    if (hs != hal::Status::Ok) return Status::FlashError;

    cur_sector_ = opens[1];
    uint32_t ate_end = 0;
    uint32_t data_end = 0;
    bool closed = false;
    scan_sector(cur_sector_, ate_end, data_end, closed);

    ate_wra_ = ate_end;
    data_wra_ = data_end;
    mounted_ = true;

    return Status::Ok;
}

// ==================== 写入 ====================

template <typename FlashT>
Status Nvs<FlashT>::append_entry(uint16_t id, const void *data,
                                 uint16_t len) {
    uint32_t wbs = flash_.write_block_size();
    uint32_t data_len = len;
#if CONFIG_NVS_DATA_CRC
    data_len += 4;
#endif
    uint32_t padded = align_up(data_len, wbs);

    uint32_t new_data = data_wra_ + padded;
    uint32_t new_ate = ate_wra_ - static_cast<uint32_t>(kAteSize);
    if (new_data > new_ate) {
        return Status::NoSpace;
    }

    if (len > 0 && data != nullptr) {
        alignas(4) uint8_t buf[512];
        uint32_t copy_len = (padded <= sizeof(buf)) ? padded : len;
        std::memcpy(buf, data, len);
#if CONFIG_NVS_DATA_CRC
        uint32_t c = crc32(static_cast<const uint8_t *>(data), len);
        std::memcpy(buf + len, &c, 4);
        if (data_len < padded) {
            std::memset(buf + data_len, 0xFF, padded - data_len);
        }
#else
        if (len < padded) {
            std::memset(buf + len, 0xFF, padded - len);
        }
#endif
        hal::Status hs = flash_.write(data_wra_, buf, copy_len);
        if (hs != hal::Status::Ok) return Status::FlashError;
    }

    Ate ate{};
    ate.id = id;
    ate.offset = static_cast<uint16_t>(data_wra_ - sector_base(cur_sector_));
    ate.len = len;
    ate.part = 0xFF;
    hal::Status hs = write_ate(new_ate, ate);
    if (hs != hal::Status::Ok) return Status::FlashError;

    data_wra_ = new_data;
    ate_wra_ = new_ate;

    return Status::Ok;
}

template <typename FlashT>
Status Nvs<FlashT>::gc_and_write(uint16_t id, const void *data,
                                 uint16_t len) {
    uint16_t old_sector = cur_sector_;
    Status s = close_sector(old_sector);
    if (s != Status::Ok) return s;

    uint16_t next = next_sector(old_sector);
    s = open_sector(next);
    if (s != Status::Ok) return s;

    uint32_t old_base = sector_base(old_sector);
    uint32_t max_ate = sector_size_ / kAteSize;

    for (uint32_t i = max_ate; i > 0; i--) {
        uint32_t addr = old_base + (i - 1) * kAteSize;
        Ate ate{};
        if (read_ate(addr, ate) != hal::Status::Ok) break;
        if (!ate_valid(ate)) break;
        if (is_close_ate(ate)) continue;
        if (ate.len == 0) continue;

        bool superseded = false;
        for (uint32_t j = i; j > 0; j--) {
            uint32_t inner = old_base + (j - 1) * kAteSize;
            if (inner == addr) continue;
            Ate inner_ate{};
            if (read_ate(inner, inner_ate) != hal::Status::Ok) break;
            if (!ate_valid(inner_ate)) break;
            if (inner_ate.id == ate.id) {
                superseded = true;
                break;
            }
        }
        if (superseded) continue;

        if (ate.len > 0) {
            uint8_t buf[256];
            uint32_t src = old_base + ate.offset;
            if (flash_.read(src, buf, ate.len) != hal::Status::Ok) {
                continue;
            }
            uint16_t saved = cur_sector_;
            cur_sector_ = next;
            Status as = append_entry(ate.id, buf, ate.len);
            cur_sector_ = saved;
            if (as != Status::Ok) return as;
        }
    }

    hal::Status hs = flash_.erase(old_base, sector_size_);
    if (hs != hal::Status::Ok) return Status::FlashError;

    cur_sector_ = next;

    if (len > 0 && data != nullptr) {
        s = append_entry(id, data, len);
        if (s != Status::Ok) return s;
    } else if (len == 0) {
        Ate tomb{};
        tomb.id = id;
        tomb.offset = 0;
        tomb.len = 0;
        tomb.part = 0xFF;
        uint32_t next_ate = ate_wra_ - static_cast<uint32_t>(kAteSize);
        if (next_ate >= data_wra_) {
            hal::Status ths = write_ate(next_ate, tomb);
            if (ths != hal::Status::Ok) return Status::FlashError;
            ate_wra_ = next_ate;
        }
    }

    return Status::Ok;
}

template <typename FlashT>
Status Nvs<FlashT>::close_sector(uint16_t idx) {
    Ate close{};
    close.id = kCloseId;
    close.offset = 0;
    close.len = 0;
    close.part = 0xFF;

    uint32_t addr = sector_base(idx) + sector_size_
                    - static_cast<uint32_t>(kAteSize);
    hal::Status hs = write_ate(addr, close);
    if (hs != hal::Status::Ok) return Status::FlashError;

    return Status::Ok;
}

template <typename FlashT>
Status Nvs<FlashT>::open_sector(uint16_t idx) {
    cur_sector_ = idx;
    data_wra_ = sector_base(idx);
    ate_wra_ = sector_base(idx) + sector_size_;
    return Status::Ok;
}

// ==================== CRC ====================

template <typename FlashT>
uint8_t Nvs<FlashT>::crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80)
                      ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                      : static_cast<uint8_t>(crc << 1);
        }
    }
    return crc;
}

template <typename FlashT>
uint32_t Nvs<FlashT>::crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320 : crc >> 1;
        }
    }
    return crc ^ 0xFFFFFFFF;
}

// ==================== 显式实例化 ====================

template class Nvs<hal::Flash>;

} // namespace nvs
