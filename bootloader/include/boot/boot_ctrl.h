#pragma once

#include <cstdint>

namespace boot {

// NVS key IDs 用于 bootloader 控制
static constexpr uint16_t NVS_BOOT_CTRL_ID      = 0xB001;
static constexpr uint16_t NVS_LOADER_UPGRADE_ID  = 0xB002;
static constexpr uint16_t NVS_COPY_PROGRESS_ID   = 0xB003;

// boot_ctrl 标志值
static constexpr uint8_t BOOT_CTRL_NORMAL       = 0x00;
static constexpr uint8_t BOOT_CTRL_ENTER_DFU    = 0x01;  // 请求进入 DFU
static constexpr uint8_t BOOT_CTRL_UPGRADE_APP  = 0x02;  // 有新 app 待拷贝 (AB 模式)
static constexpr uint8_t BOOT_CTRL_UPGRADE_LOADER = 0x03; // 有新 loader 待升级

// 读写 boot_ctrl 标志 (需要 NVS 已 mount)
bool boot_ctrl_read(uint8_t &flag);
bool boot_ctrl_write(uint8_t flag);
bool boot_ctrl_clear();

// loader_upgrade 标志
bool loader_upgrade_read(uint8_t &flag);
bool loader_upgrade_write(uint8_t flag);
bool loader_upgrade_clear();

// app 调用: 确认镜像启动成功，设置 IMAGE_F_CONFIRMED
void confirm_image();

} // namespace boot
