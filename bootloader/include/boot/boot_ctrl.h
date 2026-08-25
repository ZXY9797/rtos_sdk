#pragma once

#include <cstdint>

namespace boot {

// boot_ctrl flag values.
static constexpr uint8_t BOOT_CTRL_NORMAL = 0x00;
static constexpr uint8_t BOOT_CTRL_ENTER_DFU = 0x01;
static constexpr uint8_t BOOT_CTRL_UPGRADE_APP = 0x02;
static constexpr uint8_t BOOT_CTRL_UPGRADE_LOADER = 0x03;
static constexpr uint8_t BOOT_CTRL_TRIAL_APP = 0x04;
static constexpr uint8_t BOOT_CTRL_ROLLBACK_APP = 0x05;
static constexpr uint32_t BOOT_COPY_PROGRESS_IDLE = UINT32_MAX;

enum BootSwapPhase : uint8_t {
    BOOT_SWAP_SAVE_OLD = 0U,
    BOOT_SWAP_INSTALL_NEW = 1U,
    BOOT_SWAP_STORE_OLD = 2U,
};

bool boot_ctrl_read(uint8_t &flag);
bool boot_ctrl_write(uint8_t flag);
bool boot_ctrl_clear();

bool loader_upgrade_read(uint8_t &flag);
bool loader_upgrade_write(uint8_t flag);
bool loader_upgrade_clear();

bool boot_copy_progress_read(uint32_t &progress);
bool boot_copy_progress_write(uint32_t progress);
bool boot_copy_progress_clear();

bool boot_swap_state_read(uint32_t &progress, BootSwapPhase &phase,
                          uint32_t &length);
bool boot_swap_state_write(uint32_t progress, BootSwapPhase phase,
                           uint32_t length);
bool boot_trial_begin(uint32_t swap_length);
bool boot_rollback_begin();

[[nodiscard]] bool confirm_image();

} // namespace boot
