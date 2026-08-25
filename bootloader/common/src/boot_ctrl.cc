#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/flash_ops.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace boot {
namespace {

constexpr uint32_t kBootCtrlMagic = 0x4254434CU;      // "BTCL"
constexpr uint32_t kLegacyUpgradeMagic = 0x55504752U; // "UPGR"
constexpr uint32_t kBootCtrlVersion = 2U;
constexpr uint32_t kLegacyBootCtrlVersion = 1U;
constexpr uint32_t kMaxJournalSectorSize = 4096U;

struct BootCtrlRecord {
    uint32_t magic;
    uint32_t version;
    uint32_t sequence;
    uint8_t boot_ctrl;
    uint8_t loader_upgrade;
    uint16_t reserved0;
    uint32_t copy_progress;
    uint32_t crc32;
    uint32_t reserved1[2];
};
static_assert(sizeof(BootCtrlRecord) == 32U);

struct LegacyBootCtrlRecord {
    uint32_t magic;
    uint32_t version;
    uint8_t boot_ctrl;
    uint8_t loader_upgrade;
    uint16_t reserved0;
    uint32_t copy_progress;
    uint32_t crc32;
    uint32_t reserved1[3];
};
static_assert(sizeof(LegacyBootCtrlRecord) == 32U);

struct LegacyUpgradeFlag {
    uint32_t magic;
    uint32_t flag;
};

struct JournalState {
    bool found {false};
    BootCtrlRecord latest {};
    uint32_t latest_addr {0U};
};

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t index = 0U; index < len; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc >> 1U)
                ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

uint32_t record_crc(const BootCtrlRecord &record) {
    constexpr size_t start = offsetof(BootCtrlRecord, version);
    constexpr size_t end = offsetof(BootCtrlRecord, crc32);
    return crc32_update(
        0U, reinterpret_cast<const uint8_t *>(&record) + start,
        end - start);
}

uint32_t legacy_record_crc(const LegacyBootCtrlRecord &record) {
    return crc32_update(
        0U, reinterpret_cast<const uint8_t *>(&record),
        offsetof(LegacyBootCtrlRecord, crc32));
}

bool valid_boot_flag(uint8_t flag) {
    return flag == BOOT_CTRL_NORMAL
        || flag == BOOT_CTRL_ENTER_DFU
        || flag == BOOT_CTRL_UPGRADE_APP
        || flag == BOOT_CTRL_UPGRADE_LOADER;
}

bool valid_record(const BootCtrlRecord &record) {
    return record.magic == kBootCtrlMagic
        && record.version == kBootCtrlVersion
        && valid_boot_flag(record.boot_ctrl)
        && record.loader_upgrade <= 1U
        && record.crc32 == record_crc(record);
}

bool sequence_newer(uint32_t candidate, uint32_t reference) {
    return static_cast<int32_t>(candidate - reference) > 0;
}

void default_record(BootCtrlRecord &record) {
    record = {};
    record.magic = kBootCtrlMagic;
    record.version = kBootCtrlVersion;
    record.boot_ctrl = BOOT_CTRL_NORMAL;
    record.copy_progress = BOOT_COPY_PROGRESS_IDLE;
}

bool journal_geometry(uint32_t &base, uint32_t &size,
                      uint32_t &sector_size) {
    const auto &area = flash_area_get(FLASH_AREA_BOOT_CTRL);
    base = flash_area_addr(FLASH_AREA_BOOT_CTRL);
    size = area.size;
    sector_size = flash_erase_sector_size();
    return sector_size != 0U
        && sector_size <= kMaxJournalSectorSize
        && size >= sector_size * 2U
        && (size % sector_size) == 0U
        && (sector_size % sizeof(BootCtrlRecord)) == 0U;
}

bool scan_journal(JournalState &state) {
    uint32_t base = 0U;
    uint32_t size = 0U;
    uint32_t sector_size = 0U;
    if (!journal_geometry(base, size, sector_size)) return false;
    (void)sector_size;

    for (uint32_t offset = 0U; offset < size;
         offset += sizeof(BootCtrlRecord)) {
        BootCtrlRecord candidate {};
        if (!flash_read(base + offset, &candidate, sizeof(candidate))) {
            return false;
        }
        if (valid_record(candidate)
            && (!state.found
                || sequence_newer(candidate.sequence,
                                  state.latest.sequence))) {
            state.found = true;
            state.latest = candidate;
            state.latest_addr = base + offset;
        }
    }
    return true;
}

bool read_legacy(BootCtrlRecord &record) {
    const uint32_t address = flash_area_addr(FLASH_AREA_STORAGE);
    LegacyBootCtrlRecord legacy {};
    if (!flash_read(address, &legacy, sizeof(legacy))) return false;
    if (legacy.magic == kBootCtrlMagic
        && legacy.version == kLegacyBootCtrlVersion
        && valid_boot_flag(legacy.boot_ctrl)
        && legacy.loader_upgrade <= 1U
        && legacy.crc32 == legacy_record_crc(legacy)) {
        record.boot_ctrl = legacy.boot_ctrl;
        record.loader_upgrade = legacy.loader_upgrade;
        record.copy_progress = legacy.copy_progress;
        return true;
    }

    const auto *upgrade = reinterpret_cast<const LegacyUpgradeFlag *>(&legacy);
    if (upgrade->magic == kLegacyUpgradeMagic && upgrade->flag == 1U) {
        record.boot_ctrl = BOOT_CTRL_UPGRADE_LOADER;
        record.loader_upgrade = 1U;
        return true;
    }
    return false;
}

bool read_record(BootCtrlRecord &record) {
    default_record(record);
    JournalState state {};
    if (!scan_journal(state)) return false;
    if (state.found) {
        record = state.latest;
        return true;
    }
    (void)read_legacy(record);
    return false;
}

bool slot_erased(uint32_t address) {
    BootCtrlRecord record {};
    if (!flash_read(address, &record, sizeof(record))) return false;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&record);
    for (size_t index = 0U; index < sizeof(record); ++index) {
        if (bytes[index] != 0xFFU) return false;
    }
    return true;
}

bool commit_record(uint32_t address, BootCtrlRecord &record) {
    record.magic = kBootCtrlMagic;
    record.version = kBootCtrlVersion;
    record.crc32 = record_crc(record);

    constexpr size_t commit_offset = sizeof(record.magic);
    if (!flash_write(address + commit_offset,
                     reinterpret_cast<const uint8_t *>(&record)
                         + commit_offset,
                     sizeof(record) - commit_offset)
        || !flash_write(address, &record.magic, sizeof(record.magic))) {
        return false;
    }

    BootCtrlRecord verify {};
    return flash_read(address, &verify, sizeof(verify))
        && valid_record(verify)
        && verify.sequence == record.sequence;
}

bool write_record(BootCtrlRecord &record) {
    uint32_t base = 0U;
    uint32_t size = 0U;
    uint32_t sector_size = 0U;
    if (!journal_geometry(base, size, sector_size)) return false;

    JournalState state {};
    if (!scan_journal(state)) return false;
    record.sequence = state.found ? state.latest.sequence + 1U : 1U;

    uint32_t target_sector = 0U;
    if (state.found) {
        target_sector = (state.latest_addr - base) / sector_size;
        const uint32_t sector_base = base + target_sector * sector_size;
        for (uint32_t offset = 0U; offset < sector_size;
             offset += sizeof(BootCtrlRecord)) {
            const uint32_t address = sector_base + offset;
            if (slot_erased(address)) return commit_record(address, record);
        }
        const uint32_t sector_count = size / sector_size;
        target_sector = (target_sector + 1U) % sector_count;
    }

    const uint32_t target_address = base + target_sector * sector_size;
    // The previous sector still contains the last committed record while the
    // target sector is erased and the new record is committed.
    return flash_erase(target_address, sector_size)
        && commit_record(target_address, record);
}

} // namespace

bool boot_ctrl_read(uint8_t &flag) {
    BootCtrlRecord record {};
    const bool valid = read_record(record);
    flag = record.boot_ctrl;
    return valid;
}

bool boot_ctrl_write(uint8_t flag) {
    if (!valid_boot_flag(flag)) return false;
    BootCtrlRecord record {};
    (void)read_record(record);
    record.boot_ctrl = flag;
    if (flag == BOOT_CTRL_UPGRADE_APP) {
        record.copy_progress = 0U;
    } else if (flag == BOOT_CTRL_NORMAL) {
        record.copy_progress = BOOT_COPY_PROGRESS_IDLE;
    }
    return write_record(record);
}

bool boot_ctrl_clear() {
    return boot_ctrl_write(BOOT_CTRL_NORMAL);
}

bool loader_upgrade_read(uint8_t &flag) {
    BootCtrlRecord record {};
    const bool valid = read_record(record);
    flag = record.loader_upgrade;
    return valid;
}

bool loader_upgrade_write(uint8_t flag) {
    if (flag > 1U) return false;
    BootCtrlRecord record {};
    (void)read_record(record);
    record.loader_upgrade = flag;
    if (flag != 0U) record.boot_ctrl = BOOT_CTRL_UPGRADE_LOADER;
    return write_record(record);
}

bool loader_upgrade_clear() {
    BootCtrlRecord record {};
    (void)read_record(record);
    record.loader_upgrade = 0U;
    if (record.boot_ctrl == BOOT_CTRL_UPGRADE_LOADER) {
        record.boot_ctrl = BOOT_CTRL_NORMAL;
    }
    return write_record(record);
}

bool boot_copy_progress_read(uint32_t &progress) {
    BootCtrlRecord record {};
    const bool valid = read_record(record);
    progress = record.copy_progress;
    return valid;
}

bool boot_copy_progress_write(uint32_t progress) {
    BootCtrlRecord record {};
    (void)read_record(record);
    record.copy_progress = progress;
    return write_record(record);
}

bool boot_copy_progress_clear() {
    return boot_copy_progress_write(BOOT_COPY_PROGRESS_IDLE);
}

} // namespace boot
