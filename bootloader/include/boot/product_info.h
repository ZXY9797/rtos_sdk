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

constexpr uint32_t product_info_crc32_byte(uint32_t crc, uint8_t byte) {
    crc ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
        crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
    }
    return crc;
}

constexpr uint32_t product_info_crc32_u16(uint32_t crc, uint16_t value) {
    crc = product_info_crc32_byte(crc, static_cast<uint8_t>(value));
    return product_info_crc32_byte(crc, static_cast<uint8_t>(value >> 8));
}

constexpr uint32_t product_info_crc32_u32(uint32_t crc, uint32_t value) {
    crc = product_info_crc32_byte(crc, static_cast<uint8_t>(value));
    crc = product_info_crc32_byte(crc, static_cast<uint8_t>(value >> 8));
    crc = product_info_crc32_byte(crc, static_cast<uint8_t>(value >> 16));
    return product_info_crc32_byte(crc, static_cast<uint8_t>(value >> 24));
}

constexpr uint32_t product_info_crc32_fields(uint32_t product_id,
                                             uint32_t hw_version,
                                             ImageVersion fw_version) {
    uint32_t crc = 0xFFFFFFFFU;
    crc = product_info_crc32_u32(crc, PRODUCT_INFO_MAGIC);
    crc = product_info_crc32_u32(crc, product_id);
    crc = product_info_crc32_u32(crc, hw_version);
    crc = product_info_crc32_byte(crc, fw_version.major);
    crc = product_info_crc32_byte(crc, fw_version.minor);
    crc = product_info_crc32_u16(crc, fw_version.revision);
    crc = product_info_crc32_u32(crc, fw_version.build_num);
    for (int i = 0; i < 28; ++i) {
        crc = product_info_crc32_byte(crc, 0);
    }
    return ~crc;
}

constexpr ProductInfo make_product_info(uint32_t product_id,
                                        uint32_t hw_version,
                                        ImageVersion fw_version) {
    ProductInfo info{};
    info.magic = PRODUCT_INFO_MAGIC;
    info.product_id = product_id;
    info.hw_version = hw_version;
    info.fw_version = fw_version;
    info.crc32 = product_info_crc32_fields(product_id, hw_version, fw_version);
    return info;
}

} // namespace boot
