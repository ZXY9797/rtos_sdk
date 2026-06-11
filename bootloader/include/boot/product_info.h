#pragma once

#include <cstdint>
#include "image.h"

namespace boot {

static constexpr uint32_t PRODUCT_INFO_MAGIC  = 0x50524F44; // "PROD"
static constexpr uint32_t PRODUCT_INFO_OFFSET = 1024;       // app 偏移 1KB

struct ProductInfo {
    uint32_t     magic;            // PRODUCT_INFO_MAGIC
    uint32_t     product_id;       // 产品 ID
    uint32_t     hw_version;       // 硬件版本
    ImageVersion fw_version;       // 固件版本
    uint32_t     _reserved[10];
    uint32_t     crc32;            // CRC-32 of 前 48 bytes
};
static_assert(sizeof(ProductInfo) == 64);

// 预定义产品 ID
static constexpr uint32_t PRODUCT_ID_DEMO     = 0x0001;
static constexpr uint32_t PRODUCT_ID_DEMO_BLE = 0x0002;

} // namespace boot
