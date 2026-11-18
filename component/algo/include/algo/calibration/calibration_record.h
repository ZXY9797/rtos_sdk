#pragma once
#include <cstddef>
#include <cstdint>

namespace algo::motor {

struct CalibrationRecordType {
    uint32_t data_type;
    uint16_t version;
};

namespace calibration_records {
inline constexpr CalibrationRecordType current_linear {0x4355524CU, 2U};
inline constexpr CalibrationRecordType bus_voltage {0x56425553U, 2U};
inline constexpr CalibrationRecordType input_tmr {0x49544D52U, 4U};
inline constexpr CalibrationRecordType output_tmr {0x4F544D52U, 1U};
inline constexpr CalibrationRecordType electrical_angle {0x45414E47U, 1U};
inline constexpr CalibrationRecordType input_angle_lut {0x49414E47U, 2U};
inline constexpr CalibrationRecordType output_angle_lut {0x4F414E47U, 2U};
inline constexpr CalibrationRecordType joint_zero {0x4A5A4552U, 1U};
inline constexpr CalibrationRecordType joint_offset {0x4A4F4646U, 1U};
}  // namespace calibration_records

// The MCAL record uses a 28-byte LE header plus the native float32
// calibration value payload. Callers supply storage; this performs no I/O.
class CalibrationRecord {
public:
    static constexpr size_t header_size = 28U;
    static constexpr size_t maximum_payload_size = 1444U;
    static uint32_t crc32(const void* bytes, size_t count);
    static size_t encode(CalibrationRecordType type, uint32_t sequence,
                         const void* payload, size_t payload_size,
                         uint8_t* destination, size_t capacity);
    static bool decode(CalibrationRecordType expected, const uint8_t* record, size_t size,
                       void* payload, size_t payload_size, uint32_t* sequence = nullptr);
    static bool verify_readback(CalibrationRecordType expected,
                                const uint8_t* record, size_t size,
                                const void* payload, size_t payload_size);
};

}  // namespace algo::motor
