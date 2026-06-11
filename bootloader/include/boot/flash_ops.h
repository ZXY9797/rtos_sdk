#pragma once

#include <cstddef>
#include <cstdint>

namespace boot {

bool flash_init();
uint32_t flash_write_block_size();
uint32_t flash_erase_sector_size();

bool flash_read(uint32_t addr, void *data, size_t len);
bool flash_erase(uint32_t addr, size_t len);
bool flash_write(uint32_t addr, const void *data, size_t len);

bool flash_update(uint32_t addr, const void *data, size_t len);

} // namespace boot
