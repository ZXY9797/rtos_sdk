#pragma once

#include <cstdint>

namespace boot {

static constexpr uint32_t IMAGE_MAGIC = 0x96f3b83d;

struct ImageVersion {
    uint8_t  major;
    uint8_t  minor;
    uint16_t revision;
    uint32_t build_num;
};

struct ImageHeader {
    uint32_t     magic;            // IMAGE_MAGIC
    uint32_t     load_addr;        // 运行地址
    uint16_t     hdr_size;         // 128
    uint16_t     flags;            // ImageFlags
    uint32_t     img_size;         // payload 大小 (不含 header)
    ImageVersion version;
    uint32_t     _pad1;
    uint8_t      sha256[32];       // SHA-256(前64B header + payload)
    uint8_t      _pad2[32];        // 预留签名空间
    uint8_t      _reserved[36];    // 对齐到 128
};
static_assert(sizeof(ImageHeader) == 128);

enum ImageFlags : uint16_t {
    IMAGE_F_CONFIRMED    = 0x0001, // app 已确认启动成功
    IMAGE_F_PENDING      = 0x0002, // 等待确认 (测试启动)
    IMAGE_F_NON_BOOTABLE = 0x0004, // 不可启动
};

} // namespace boot
