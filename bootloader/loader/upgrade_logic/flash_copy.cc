#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <boot/boot_ctrl.h>
#include <boot/image.h>
#include <boot/runtime.h>

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace boot {
namespace {

constexpr uint32_t kCopyCheckpointBytes = 32768U;
constexpr uint32_t kCopyBufferSize = 256U;

static uint8_t g_copy_data[kCopyBufferSize];
static uint8_t g_left_data[kCopyBufferSize];
static uint8_t g_right_data[kCopyBufferSize];

bool regions_equal(uint32_t left, uint32_t right, uint32_t len) {
    uint32_t done = 0U;
    while (done < len) {
        const uint32_t chunk = std::min<uint32_t>(
            kCopyBufferSize, len - done);
        if (!flash_read(left + done, g_left_data, chunk)
            || !flash_read(right + done, g_right_data, chunk)
            || std::memcmp(g_left_data, g_right_data, chunk) != 0) {
            return false;
        }
        done += chunk;
        watchdog_service();
    }
    return true;
}

bool valid_progress(uint32_t progress, uint32_t copy_len,
                    uint32_t sector_size) {
    return progress <= copy_len
        && (progress == copy_len || (progress % sector_size) == 0U);
}

bool copy_sector(uint32_t src, uint32_t dst, uint32_t data_len,
                 uint32_t sector_size) {
    watchdog_service();
    if (!flash_erase(dst, sector_size)) {
        return false;
    }

    uint32_t done = 0U;
    while (done < data_len) {
        const uint32_t chunk = std::min<uint32_t>(
            kCopyBufferSize, data_len - done);
        if (!flash_read(src + done, g_copy_data, chunk)
            || !flash_write(dst + done, g_copy_data, chunk)) {
            return false;
        }
        done += chunk;
        watchdog_service();
    }
    return true;
}

} // namespace

bool flash_copy_upgrade_to_slot0() {
    const auto &src_area = flash_area_get(FLASH_AREA_UPGRADE);
    const auto &dst_area = flash_area_get(FLASH_AREA_SLOT0);
    const uint32_t sector_size = flash_erase_sector_size();

    ImageHeader hdr{};
    const uint32_t src_base = flash_area_addr(FLASH_AREA_UPGRADE);
    if (!flash_read(src_base, &hdr, sizeof(hdr))) return false;
    if (hdr.magic != IMAGE_MAGIC ||
        hdr.hdr_size != sizeof(ImageHeader) ||
        hdr.img_size == 0) {
        return false;
    }

    const uint64_t copy_len64 =
        static_cast<uint64_t>(hdr.hdr_size) + hdr.img_size;
    if (sector_size == 0U
        || (kCopyCheckpointBytes % sector_size) != 0U
        || (src_area.size % sector_size) != 0U
        || (dst_area.size % sector_size) != 0U
        || copy_len64 > src_area.size || copy_len64 > dst_area.size) {
        return false;
    }

    const uint32_t copy_len = static_cast<uint32_t>(copy_len64);
    const uint32_t dst_base = flash_area_addr(FLASH_AREA_SLOT0);
    const uint64_t src_end = static_cast<uint64_t>(src_base) + copy_len;
    const uint64_t dst_end = static_cast<uint64_t>(dst_base) + copy_len;
    if (src_base < dst_end && dst_base < src_end) {
        return false;
    }

    uint32_t progress = 0U;
    if (!boot_copy_progress_read(progress)
        || progress == BOOT_COPY_PROGRESS_IDLE
        || !valid_progress(progress, copy_len, sector_size)
        || (progress > 0U
            && !regions_equal(src_base, dst_base, progress))) {
        progress = 0U;
        if (!boot_copy_progress_write(progress)) {
            return false;
        }
    }

    for (uint32_t offset = progress; offset < copy_len;
         offset += sector_size) {
        const uint32_t chunk = std::min(sector_size, copy_len - offset);
        if (!copy_sector(src_base + offset, dst_base + offset,
                         chunk, sector_size)
            || !regions_equal(src_base + offset,
                              dst_base + offset, chunk)) {
            return false;
        }

        const uint32_t next = offset + chunk;
        if ((next % kCopyCheckpointBytes) == 0U || next == copy_len) {
            if (!boot_copy_progress_write(next)) {
                return false;
            }
        }
    }

    return true;
}

} // namespace boot
