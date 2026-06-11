#include <cstdint>
#include <cstring>

namespace upgrade {

int flash_erase_sector(uint32_t addr, uint32_t size) {
    // TODO: HAL flash erase
    (void)addr;
    (void)size;
    return 0;
}

int flash_write(uint32_t addr, const uint8_t *data, uint32_t len) {
    // TODO: HAL flash write (4-byte aligned)
    (void)addr;
    (void)data;
    (void)len;
    return 0;
}

} // namespace upgrade
