#pragma once

#include <cstdint>
#include <cstddef>

namespace nvs {

inline constexpr uint16_t kMaxId = 0xFFFE;
inline constexpr uint16_t kCloseId = 0xFFFF;
inline constexpr size_t kAteSize = 8;
inline constexpr uint32_t kBlockSize = 32;

// ATE (Allocation Table Entry) — 8 字节，packed
//
// on-flash layout (little-endian):
//   [0..1]  id       (2B)
//   [2..3]  offset   (2B)  扇区内数据偏移
//   [4..5]  len      (2B)  数据长度（0=墓碑/删除）
//   [6]     part     (1B)  保留 (0xFF)
//   [7]     crc8     (1B)  CRC-8 校验前 7 字节
struct __attribute__((packed)) Ate {
    uint16_t id;
    uint16_t offset;
    uint16_t len;
    uint8_t  part {0xFF};
    uint8_t  crc8;
};

static_assert(sizeof(Ate) == kAteSize, "Ate must be 8 bytes");

enum class Status : uint8_t {
    Ok = 0,
    NotFound,
    CrcError,
    NoSpace,
    FlashError,
    NotMounted,
    InvalidArgument,
};

} // namespace nvs
