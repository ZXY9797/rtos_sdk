#include <boot/flash_map.h>
#include <boot/flash_ops.h>
#include <drivers/flash.h>
#include <drivers/status.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace boot {
namespace {

constexpr size_t kMaxBootSectorSize = 4096;

#if defined(CONFIG_FLASH_GD32) || defined(CONFIG_FLASH_GOODIX) || defined(CONFIG_FLASH_STM32)
hal::Flash &flash_device() {
    static hal::Flash flash(hal::flash_create_default());
    return flash;
}

bool ensure_flash() {
    auto &flash = flash_device();
    if (flash.is_initialized()) return true;
    return flash.init() == hal::Status::Ok;
}

bool addr_to_offset(uint32_t addr, size_t len, uint32_t &offset) {
    const uint32_t base = flash_base_addr();
    if (addr < base) return false;
    offset = addr - base;
    if (len == 0) return true;
    return static_cast<uint64_t>(offset) + len <= UINT32_MAX;
}
#endif

} // namespace

bool flash_init() {
#if defined(CONFIG_FLASH_GD32) || defined(CONFIG_FLASH_GOODIX) || defined(CONFIG_FLASH_STM32)
    return ensure_flash();
#else
    return false;
#endif
}

uint32_t flash_write_block_size() {
#if defined(CONFIG_FLASH_GD32) || defined(CONFIG_FLASH_GOODIX) || defined(CONFIG_FLASH_STM32)
    return flash_device().write_block_size();
#else
    return 0;
#endif
}

uint32_t flash_erase_sector_size() {
#if defined(CONFIG_FLASH_GD32) || defined(CONFIG_FLASH_GOODIX) || defined(CONFIG_FLASH_STM32)
    return flash_device().erase_sector_size();
#else
    return 0;
#endif
}

bool flash_read(uint32_t addr, void *data, size_t len) {
#if defined(CONFIG_FLASH_GD32) || defined(CONFIG_FLASH_GOODIX) || defined(CONFIG_FLASH_STM32)
    if (!data || len == 0 || !ensure_flash()) return false;
    uint32_t offset = 0;
    if (!addr_to_offset(addr, len, offset)) return false;
    return flash_device().read(offset, data, len) == hal::Status::Ok;
#else
    (void)addr;
    (void)data;
    (void)len;
    return false;
#endif
}

bool flash_erase(uint32_t addr, size_t len) {
#if defined(CONFIG_FLASH_GD32) || defined(CONFIG_FLASH_GOODIX) || defined(CONFIG_FLASH_STM32)
    if (len == 0 || !ensure_flash()) return false;
    uint32_t offset = 0;
    if (!addr_to_offset(addr, len, offset)) return false;
    return flash_device().erase(offset, len) == hal::Status::Ok;
#else
    (void)addr;
    (void)len;
    return false;
#endif
}

bool flash_write(uint32_t addr, const void *data, size_t len) {
#if defined(CONFIG_FLASH_GD32) || defined(CONFIG_FLASH_GOODIX) || defined(CONFIG_FLASH_STM32)
    if (!data || len == 0 || !ensure_flash()) return false;
    uint32_t offset = 0;
    if (!addr_to_offset(addr, len, offset)) return false;
    return flash_device().write(offset, data, len) == hal::Status::Ok;
#else
    (void)addr;
    (void)data;
    (void)len;
    return false;
#endif
}

bool flash_update(uint32_t addr, const void *data, size_t len) {
#if defined(CONFIG_FLASH_GD32) || defined(CONFIG_FLASH_GOODIX) || defined(CONFIG_FLASH_STM32)
    if (!data || len == 0 || !ensure_flash()) return false;

    const uint32_t sector_size = flash_device().erase_sector_size();
    if (sector_size == 0 || sector_size > kMaxBootSectorSize) return false;

    uint32_t start_offset = 0;
    if (!addr_to_offset(addr, len, start_offset)) return false;

    const auto *src = static_cast<const uint8_t *>(data);
    size_t done = 0;
    static uint8_t sector[kMaxBootSectorSize];

    while (done < len) {
        const uint32_t cur_offset = start_offset + static_cast<uint32_t>(done);
        const uint32_t sector_offset = cur_offset - (cur_offset % sector_size);
        const uint32_t sector_base = flash_base_addr() + sector_offset;
        const size_t in_sector = cur_offset - sector_offset;
        const size_t chunk = std::min(len - done, static_cast<size_t>(sector_size - in_sector));

        if (!flash_read(sector_base, sector, sector_size)) return false;
        std::memcpy(sector + in_sector, src + done, chunk);
        if (!flash_erase(sector_base, sector_size)) return false;
        if (!flash_write(sector_base, sector, sector_size)) return false;

        done += chunk;
    }

    return true;
#else
    (void)addr;
    (void)data;
    (void)len;
    return false;
#endif
}

} // namespace boot
