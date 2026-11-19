#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <cmath>



namespace algo::motor {

inline constexpr size_t kAngleLutPointCount = 360U;
inline constexpr size_t kAngleLutChunkPoints = 120U;
inline constexpr uint8_t kAngleLutWireVersion = 2U;
inline constexpr size_t kAngleLutRequestHeaderBytes = 12U;
inline constexpr size_t kAngleLutResponseHeaderBytes = 16U;

// Identical persisted layout for input and output. Wire values are encoded LE.
struct MotorAngleLutData {
    bool valid {false};
    uint8_t reserved[3] {};
    float angle_offset_rad[kAngleLutPointCount] {};
};
static_assert(sizeof(MotorAngleLutData) == 1444U);

// Stable shared handle. Only the slow task writes it, under an ISR critical section.
// The writer detaches it before copying the sole output array outside that section.
struct MotorAngleLutView {
    const MotorAngleLutData* data {nullptr};
    bool updating {false};
};

enum class AngleLutTable : uint8_t { Output = 0U, Input = 1U };
enum class AngleLutAction : uint8_t {
    Start = 1U, Data = 2U, Finish = 3U, Abort = 4U, ReadBegin = 5U, Read = 6U,
};

struct AngleLutRequest {
    AngleLutTable table {};
    AngleLutAction action {};
    uint32_t session {0U};
    uint16_t index {0U};
    uint16_t count {0U};
    uint32_t crc {0U};
    const uint8_t* values {nullptr}; // Only used synchronously while accepting Data.
};

inline uint16_t lut_read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8U));
}
inline uint32_t lut_read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8U) |
           (static_cast<uint32_t>(p[2]) << 16U) | (static_cast<uint32_t>(p[3]) << 24U);
}
inline bool decode_angle_lut_request(const uint8_t* bytes, size_t size, AngleLutRequest* out) {
    if (!bytes || !out || size < kAngleLutRequestHeaderBytes ||
        bytes[0] != kAngleLutWireVersion || bytes[1] > 1U || bytes[3] != 0U ||
        bytes[2] < 1U || bytes[2] > 6U) return false;
    *out = {static_cast<AngleLutTable>(bytes[1]), static_cast<AngleLutAction>(bytes[2]),
            lut_read_u32(bytes+4), lut_read_u16(bytes+8), lut_read_u16(bytes+10)};
    if (out->session == 0U) return false;
    switch (out->action) {
    case AngleLutAction::Start:
        if (size != 16U || out->index != 0U || out->count != kAngleLutPointCount) return false;
        out->crc = lut_read_u32(bytes+12); return true;
    case AngleLutAction::Data:
    case AngleLutAction::Read:
        if (!out->count || out->count > kAngleLutChunkPoints ||
            out->index >= kAngleLutPointCount || out->count > kAngleLutPointCount-out->index)
            return false;
        if (size != (out->action == AngleLutAction::Data ? 12U+4U*out->count : 12U)) return false;
        out->values = (out->action == AngleLutAction::Data) ? bytes+12 : nullptr;
        return true;
    default:
        return size == 12U && out->index == 0U && out->count == 0U;
    }
}

struct AngleLutTransfer {
    MotorAngleLutData data {};
    uint8_t received[(kAngleLutPointCount+7U)/8U] {};
    uint32_t session {0U};
    uint32_t crc {0U};
    uint32_t last_ms {0U};
    uint16_t owner {0U};
    AngleLutTable table {};
    bool read_only {false};
    bool committed {false};

    void begin(const AngleLutRequest& request, uint16_t source, uint32_t now) {
        data = {}; std::memset(received, 0, sizeof(received));
        session=request.session; crc=request.crc; owner=source; table=request.table;
        last_ms=now; read_only=false; committed=false;
    }
    bool matches(const AngleLutRequest& request, uint16_t source) const {
        return session != 0U && session==request.session && owner==source && table==request.table;
    }
    // Validate the entire chunk before modifying any part of the staging table.
    bool accept(const AngleLutRequest& request) {
        if (!request.values || !request.count || request.count > kAngleLutChunkPoints ||
            request.index >= kAngleLutPointCount || request.count > kAngleLutPointCount-request.index)
            return false;
        for (size_t j=0; j<request.count; ++j) {
            const uint32_t bits=lut_read_u32(request.values+4*j);
            float value; std::memcpy(&value,&bits,4);
            const size_t i=request.index+j;
            if (!std::isfinite(value) || ((received[i/8] & (1U<<(i%8))) &&
                std::memcmp(&data.angle_offset_rad[i],&value,4)!=0)) return false;
        }
        for (size_t j=0; j<request.count; ++j) {
            const uint32_t bits=lut_read_u32(request.values+4*j);
            const size_t i=request.index+j;
            std::memcpy(&data.angle_offset_rad[i],&bits,4);
            received[i/8] |= static_cast<uint8_t>(1U<<(i%8));
        }
        return true;
    }
    uint32_t checksum() const {
        // Supported MCU and host-test targets are little endian IEEE754 float32.
        uint32_t computed_crc = 0xFFFFFFFFU;
        const auto* bytes = reinterpret_cast<const uint8_t*>(data.angle_offset_rad);
        for (size_t i = 0; i < sizeof(data.angle_offset_rad); ++i) {
            computed_crc ^= bytes[i];
            for (unsigned bit = 0; bit < 8; ++bit)
                computed_crc = (computed_crc >> 1U) ^ ((computed_crc & 1U) ? 0xEDB88320U : 0U);
        }
        return computed_crc ^ 0xFFFFFFFFU;
    }
    bool complete() const {
        for (auto mask : received) if (mask != 0xFFU) return false;
        return checksum()==crc;
    }
};

} // namespace algo::motor
