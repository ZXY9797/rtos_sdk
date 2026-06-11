#pragma once

#include <cstdint>

namespace boot {

// NVS key IDs used by bootloader control.
static constexpr uint16_t NVS_BOOT_CTRL_ID = 0xB001;
static constexpr uint16_t NVS_LOADER_UPGRADE_ID = 0xB002;
static constexpr uint16_t NVS_COPY_PROGRESS_ID = 0xB003;

// boot_ctrl flag values.
static constexpr uint8_t BOOT_CTRL_NORMAL = 0x00;
static constexpr uint8_t BOOT_CTRL_ENTER_DFU = 0x01;
static constexpr uint8_t BOOT_CTRL_UPGRADE_APP = 0x02;
static constexpr uint8_t BOOT_CTRL_UPGRADE_LOADER = 0x03;

bool boot_ctrl_read(uint8_t &flag);
bool boot_ctrl_write(uint8_t flag);
bool boot_ctrl_clear();

bool loader_upgrade_read(uint8_t &flag);
bool loader_upgrade_write(uint8_t flag);
bool loader_upgrade_clear();

void confirm_image();

} // namespace boot
