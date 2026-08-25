#pragma once

#include <cstddef>
#include <cstdint>

namespace nvs {

inline constexpr uint16_t kMaxId = 0xFFFEU;
inline constexpr uint16_t kCloseId = 0xFFFFU;
inline constexpr size_t kAteSize = 8U;

// Allocation descriptor. Data grows upward while descriptors grow downward.
// part=0xA5 and crc8 authenticate this immutable descriptor; a separate
// write-block-sized marker commits the record after its payload is durable.
struct __attribute__((packed)) Ate {
    uint16_t id {};
    uint16_t offset {};
    uint16_t len {};
    uint8_t part {0xFFU};
    uint8_t crc8 {0xFFU};
};

static_assert(sizeof(Ate) == kAteSize);

enum class Status : uint8_t {
    Ok = 0U,
    NotFound,
    CrcError,
    NoSpace,
    FlashError,
    NotMounted,
    InvalidArgument,
    MigrationRequired,
};

} // namespace nvs
