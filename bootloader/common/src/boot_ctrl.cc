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
constexpr uint32_t kBootCtrlVersion = 1;
constexpr uint32_t kErasedWord = 0xFFFFFFFFU;

struct BootCtrlRecord {
    uint32_t magic;
    uint32_t version;
    uint8_t boot_ctrl;
    uint8_t loader_upgrade;
    uint16_t _reserved0;
    uint32_t copy_progress;
    uint32_t crc32;
    uint32_t _reserved1[3];
};

static_assert(sizeof(BootCtrlRecord) == 32);

struct LegacyUpgradeFlag {
    uint32_t magic;
    uint32_t flag;
};

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    crc = ~crc;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return ~crc;
}

uint32_t record_crc(const BootCtrlRecord &record) {
    return crc32_update(0, reinterpret_cast<const uint8_t *>(&record),
                        offsetof(BootCtrlRecord, crc32));
}

uint32_t record_addr() {
    return flash_area_addr(FLASH_AREA_STORAGE);
}

bool read_record(BootCtrlRecord &record) {
    if (!flash_read(record_addr(), &record, sizeof(record))) {
        std::memset(&record, 0, sizeof(record));
    }

    if (record.magic != kBootCtrlMagic ||
        record.version != kBootCtrlVersion ||
        record.crc32 != record_crc(record)) {
        std::memset(&record, 0, sizeof(record));
        record.magic = kBootCtrlMagic;
        record.version = kBootCtrlVersion;
        record.boot_ctrl = BOOT_CTRL_NORMAL;
        record.loader_upgrade = 0;
        record.copy_progress = 0;
        record.crc32 = record_crc(record);
        return false;
    }
    return true;
}

bool write_record(BootCtrlRecord &record) {
    record.magic = kBootCtrlMagic;
    record.version = kBootCtrlVersion;
    record.crc32 = record_crc(record);
    return flash_update(record_addr(), &record, sizeof(record));
}

bool legacy_loader_upgrade_pending() {
    LegacyUpgradeFlag legacy{};
    if (!flash_read(record_addr(), &legacy, sizeof(legacy))) return false;
    return legacy.magic == kLegacyUpgradeMagic && legacy.flag == 1;
}

} // namespace

bool boot_ctrl_read(uint8_t &flag) {
    BootCtrlRecord record{};
    const bool valid = read_record(record);
    flag = record.boot_ctrl;
    if (!valid && legacy_loader_upgrade_pending()) {
        flag = BOOT_CTRL_UPGRADE_LOADER;
    }
    return valid;
}

bool boot_ctrl_write(uint8_t flag) {
    BootCtrlRecord record{};
    (void)read_record(record);
    record.boot_ctrl = flag;
    return write_record(record);
}

bool boot_ctrl_clear() {
    return boot_ctrl_write(BOOT_CTRL_NORMAL);
}

bool loader_upgrade_read(uint8_t &flag) {
    BootCtrlRecord record{};
    const bool valid = read_record(record);
    flag = record.loader_upgrade;
    if (!valid && legacy_loader_upgrade_pending()) {
        flag = 1;
    }
    return valid;
}

bool loader_upgrade_write(uint8_t flag) {
    BootCtrlRecord record{};
    (void)read_record(record);
    record.loader_upgrade = flag;
    if (flag) record.boot_ctrl = BOOT_CTRL_UPGRADE_LOADER;
    return write_record(record);
}

bool loader_upgrade_clear() {
    BootCtrlRecord record{};
    (void)read_record(record);
    record.loader_upgrade = 0;
    if (record.boot_ctrl == BOOT_CTRL_UPGRADE_LOADER) {
        record.boot_ctrl = BOOT_CTRL_NORMAL;
    }
    record.copy_progress = kErasedWord;
    return write_record(record);
}

} // namespace boot
