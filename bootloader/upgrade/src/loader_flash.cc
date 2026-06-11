#include <boot/flash_ops.h>

#include <cstdint>

namespace upgrade {

int flash_erase_sector(uint32_t addr, uint32_t size) {
    return boot::flash_erase(addr, size) ? 0 : -1;
}

int flash_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    return boot::flash_write(addr, data, len) ? 0 : -1;
}

} // namespace upgrade
