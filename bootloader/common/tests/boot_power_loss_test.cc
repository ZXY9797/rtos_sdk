#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace boot {
bool flash_copy_upgrade_to_slot0();
}

namespace {

constexpr uint32_t kFlashBase = 0x08000000U;
constexpr uint32_t kFlashSize = 0x00060000U;
constexpr uint32_t kSectorSize = 0x800U;
constexpr uint32_t kSlot0Offset = 0xA000U;
constexpr uint32_t kUpgradeOffset = 0x32000U;
constexpr uint32_t kStorageOffset = 0x58000U;
constexpr uint32_t kBootCtrlOffset = 0x5F000U;

std::vector<uint8_t> g_flash(kFlashSize, 0xFFU);
bool g_fail_next_commit_word = false;
uint32_t g_fail_slot_write_at = UINT32_MAX;

bool range_valid(uint32_t address, size_t length) {
    return address >= kFlashBase
        && static_cast<uint64_t>(address - kFlashBase) + length
            <= g_flash.size();
}

size_t offset_of(uint32_t address) {
    return static_cast<size_t>(address - kFlashBase);
}

void reset_flash() {
    std::fill(g_flash.begin(), g_flash.end(), 0xFFU);
    g_fail_next_commit_word = false;
    g_fail_slot_write_at = UINT32_MAX;
}

void test_journal_commit_and_storage_isolation() {
    reset_flash();
    std::fill_n(g_flash.begin() + kStorageOffset, kSectorSize, 0xA5U);
    const std::vector<uint8_t> storage_before(
        g_flash.begin() + kStorageOffset,
        g_flash.begin() + kStorageOffset + kSectorSize);

    assert(boot::boot_ctrl_write(boot::BOOT_CTRL_ENTER_DFU));
    uint8_t flag = 0U;
    assert(boot::boot_ctrl_read(flag));
    assert(flag == boot::BOOT_CTRL_ENTER_DFU);

    g_fail_next_commit_word = true;
    assert(!boot::boot_ctrl_write(boot::BOOT_CTRL_UPGRADE_APP));
    assert(boot::boot_ctrl_read(flag));
    assert(flag == boot::BOOT_CTRL_ENTER_DFU);

    assert(boot::boot_ctrl_write(boot::BOOT_CTRL_UPGRADE_APP));
    uint32_t progress = UINT32_MAX;
    assert(boot::boot_copy_progress_read(progress));
    assert(progress == 0U);

    for (uint32_t index = 1U; index <= 140U; ++index) {
        assert(boot::boot_copy_progress_write(index * 2048U));
    }
    assert(boot::boot_copy_progress_read(progress));
    assert(progress == 140U * 2048U);
    assert(std::equal(storage_before.begin(), storage_before.end(),
                      g_flash.begin() + kStorageOffset));
}

void test_copy_resumes_from_committed_checkpoint() {
    reset_flash();
    constexpr uint32_t payload_size = 70000U;
    boot::ImageHeader header {};
    header.magic = boot::IMAGE_MAGIC;
    header.hdr_size = sizeof(header);
    header.img_size = payload_size;
    header.flags = boot::IMAGE_F_PENDING;

    const size_t staging = kUpgradeOffset;
    std::memcpy(g_flash.data() + staging, &header, sizeof(header));
    for (uint32_t index = 0U; index < payload_size; ++index) {
        g_flash[staging + sizeof(header) + index] =
            static_cast<uint8_t>((index * 17U + 3U) & 0xFFU);
    }

    assert(boot::boot_ctrl_write(boot::BOOT_CTRL_UPGRADE_APP));
    g_fail_slot_write_at = kFlashBase + kSlot0Offset + 20U * kSectorSize;
    assert(!boot::flash_copy_upgrade_to_slot0());

    uint32_t progress = 0U;
    assert(boot::boot_copy_progress_read(progress));
    assert(progress == 32768U);

    g_fail_slot_write_at = UINT32_MAX;
    assert(boot::flash_copy_upgrade_to_slot0());
    const size_t copy_size = sizeof(header) + payload_size;
    assert(std::equal(g_flash.begin() + kUpgradeOffset,
                      g_flash.begin() + kUpgradeOffset + copy_size,
                      g_flash.begin() + kSlot0Offset));
    assert(boot::boot_copy_progress_read(progress));
    assert(progress == copy_size);
}

} // namespace

namespace boot {

void watchdog_service() {}

uint32_t flash_base_addr() {
    return kFlashBase;
}

const FlashArea &flash_area_get(FlashAreaId id) {
    static const FlashArea areas[FLASH_AREA_COUNT] = {
        {0x00000U, 0x04000U},
        {0x04000U, 0x06000U},
        {kSlot0Offset, 0x28000U},
        {kUpgradeOffset, 0x26000U},
        {kStorageOffset, 0x07000U},
        {kBootCtrlOffset, 0x01000U},
    };
    return areas[id];
}

bool flash_init() { return true; }
uint32_t flash_write_block_size() { return 4U; }
uint32_t flash_erase_sector_size() { return kSectorSize; }

bool flash_read(uint32_t address, void *data, size_t length) {
    if (data == nullptr || length == 0U || !range_valid(address, length)) {
        return false;
    }
    std::memcpy(data, g_flash.data() + offset_of(address), length);
    return true;
}

bool flash_erase(uint32_t address, size_t length) {
    if (!range_valid(address, length) || length == 0U
        || ((address - kFlashBase) % kSectorSize) != 0U
        || (length % kSectorSize) != 0U) {
        return false;
    }
    std::fill_n(g_flash.begin() + offset_of(address), length, 0xFFU);
    return true;
}

bool flash_write(uint32_t address, const void *data, size_t length) {
    if (data == nullptr || length == 0U || !range_valid(address, length)) {
        return false;
    }
    const uint32_t control_base = kFlashBase + kBootCtrlOffset;
    if (g_fail_next_commit_word && length == sizeof(uint32_t)
        && address >= control_base
        && address < control_base + 0x1000U
        && ((address - control_base) % 32U) == 0U) {
        g_fail_next_commit_word = false;
        return false;
    }
    if (address >= g_fail_slot_write_at
        && address < kFlashBase + kSlot0Offset + 0x28000U) {
        return false;
    }

    const auto *source = static_cast<const uint8_t *>(data);
    const size_t offset = offset_of(address);
    for (size_t index = 0U; index < length; ++index) {
        if ((g_flash[offset + index] & source[index]) != source[index]) {
            return false;
        }
    }
    for (size_t index = 0U; index < length; ++index) {
        g_flash[offset + index] &= source[index];
    }
    return true;
}

bool flash_update(uint32_t, const void *, size_t) {
    return false;
}

} // namespace boot

int main() {
    test_journal_commit_and_storage_isolation();
    test_copy_resumes_from_committed_checkpoint();
    std::cout << "boot power-loss tests passed\n";
    return 0;
}
