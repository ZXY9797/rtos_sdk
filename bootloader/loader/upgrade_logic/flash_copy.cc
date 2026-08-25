#include <boot/boot_ctrl.h>
#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/image.h>
#include <boot/runtime.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace boot {
namespace {

constexpr uint32_t kCopyBufferSize = 256U;
uint8_t copy_data[kCopyBufferSize] {};
uint8_t verify_data[kCopyBufferSize] {};

bool image_extent(uint32_t base, const FlashArea& area, uint32_t& extent)
{
    ImageHeader header {};
    if (!flash_read(base, &header, sizeof(header))
        || header.magic != IMAGE_MAGIC
        || header.hdr_size != sizeof(ImageHeader)
        || header.img_size == 0U) {
        return false;
    }
    const uint64_t total = static_cast<uint64_t>(header.hdr_size)
        + header.img_size;
    if (total > area.size) {
        return false;
    }
    extent = static_cast<uint32_t>(total);
    return true;
}

bool regions_equal(uint32_t left, uint32_t right, uint32_t length)
{
    uint32_t done = 0U;
    while (done < length) {
        const uint32_t chunk = std::min<uint32_t>(
            kCopyBufferSize, length - done);
        if (!flash_read(left + done, copy_data, chunk)
            || !flash_read(right + done, verify_data, chunk)
            || std::memcmp(copy_data, verify_data, chunk) != 0) {
            return false;
        }
        done += chunk;
        watchdog_service();
    }
    return true;
}

bool replace_sector(uint32_t source, uint32_t destination,
                    uint32_t sector_size)
{
    watchdog_service();
    if (!flash_erase(destination, sector_size)) {
        return false;
    }
    for (uint32_t offset = 0U; offset < sector_size;
         offset += kCopyBufferSize) {
        const uint32_t chunk = std::min<uint32_t>(
            kCopyBufferSize, sector_size - offset);
        if (!flash_read(source + offset, copy_data, chunk)
            || !flash_write(destination + offset, copy_data, chunk)) {
            return false;
        }
        watchdog_service();
    }
    return regions_equal(source, destination, sector_size);
}

bool disjoint(uint32_t left, uint32_t left_size,
              uint32_t right, uint32_t right_size)
{
    const uint64_t left_end = static_cast<uint64_t>(left) + left_size;
    const uint64_t right_end = static_cast<uint64_t>(right) + right_size;
    return left_end <= right || right_end <= left;
}

} // namespace

bool flash_copy_upgrade_to_slot0()
{
    const FlashArea& upgrade = flash_area_get(FLASH_AREA_UPGRADE);
    const FlashArea& slot = flash_area_get(FLASH_AREA_SLOT0);
    const FlashArea& scratch = flash_area_get(FLASH_AREA_SCRATCH);
    const uint32_t sector_size = flash_erase_sector_size();
    const uint32_t upgrade_base = flash_area_addr(FLASH_AREA_UPGRADE);
    const uint32_t slot_base = flash_area_addr(FLASH_AREA_SLOT0);
    const uint32_t scratch_base = flash_area_addr(FLASH_AREA_SCRATCH);

    uint32_t upgrade_extent = 0U;
    uint32_t slot_extent = 0U;
    const bool upgrade_header_valid =
        image_extent(upgrade_base, upgrade, upgrade_extent);
    const bool slot_header_valid = image_extent(slot_base, slot, slot_extent);
    if (sector_size == 0U || scratch.size != sector_size
        || (!upgrade_header_valid && !slot_header_valid)
        || !disjoint(upgrade_base, upgrade.size, slot_base, slot.size)
        || !disjoint(upgrade_base, upgrade.size, scratch_base, scratch.size)
        || !disjoint(slot_base, slot.size, scratch_base, scratch.size)) {
        return false;
    }

    if (!upgrade_header_valid) {
        upgrade_extent = slot_extent;
    }
    if (!slot_header_valid) {
        slot_extent = upgrade_extent;
    }
    const uint32_t maximum_extent = std::max(upgrade_extent, slot_extent);
    const uint64_t aligned =
        (static_cast<uint64_t>(maximum_extent) + sector_size - 1U)
        / sector_size * sector_size;
    if (aligned == 0U || aligned > slot.size || aligned > upgrade.size) {
        return false;
    }
    uint32_t swap_length = static_cast<uint32_t>(aligned);

    uint32_t progress = 0U;
    uint32_t recorded_length = 0U;
    BootSwapPhase phase = BOOT_SWAP_SAVE_OLD;
    const bool state_read =
        boot_swap_state_read(progress, phase, recorded_length);
    const bool initial_state = state_read && progress == 0U
        && phase == BOOT_SWAP_SAVE_OLD && recorded_length == 0U;
    const bool resumable_state = state_read
        && progress != BOOT_COPY_PROGRESS_IDLE
        && recorded_length > 0U
        && recorded_length <= slot.size
        && recorded_length <= upgrade.size
        && (recorded_length % sector_size) == 0U
        && progress <= recorded_length
        && (progress % sector_size) == 0U
        && phase <= BOOT_SWAP_STORE_OLD;
    if (resumable_state) {
        // A header can be temporarily invalid while its sector is being
        // replaced. The CRC-protected length recorded before the first erase
        // is authoritative until the swap completes.
        swap_length = recorded_length;
    } else {
        // With a committed non-initial state, one header may be temporarily
        // invalid only when the recorded phase proves how to reconstruct it.
        // Never infer that recovery phase from mutable Flash contents alone.
        if (!initial_state || !upgrade_header_valid) {
            return false;
        }
        progress = 0U;
        phase = BOOT_SWAP_SAVE_OLD;
        if (!boot_swap_state_write(progress, phase, swap_length)) {
            return false;
        }
    }

    while (progress < swap_length) {
        const uint32_t slot_sector = slot_base + progress;
        const uint32_t upgrade_sector = upgrade_base + progress;
        if (phase == BOOT_SWAP_SAVE_OLD) {
            if (!replace_sector(slot_sector, scratch_base, sector_size)
                || !boot_swap_state_write(
                    progress, BOOT_SWAP_INSTALL_NEW, swap_length)) {
                return false;
            }
            phase = BOOT_SWAP_INSTALL_NEW;
        }
        if (phase == BOOT_SWAP_INSTALL_NEW) {
            if (!replace_sector(upgrade_sector, slot_sector, sector_size)
                || !boot_swap_state_write(
                    progress, BOOT_SWAP_STORE_OLD, swap_length)) {
                return false;
            }
            phase = BOOT_SWAP_STORE_OLD;
        }
        if (phase == BOOT_SWAP_STORE_OLD) {
            if (!replace_sector(scratch_base, upgrade_sector, sector_size)) {
                return false;
            }
            progress += sector_size;
            phase = BOOT_SWAP_SAVE_OLD;
            if (!boot_swap_state_write(progress, phase, swap_length)) {
                return false;
            }
        }
    }
    return true;
}

} // namespace boot
