#include <algo/calibration/calibration_record.h>
#include <cstring>

namespace algo::motor {
namespace {
uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(uint16_t(p[1]) << 8U); }
uint32_t read32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8U) |
           (uint32_t(p[2]) << 16U) | (uint32_t(p[3]) << 24U);
}
void write16(uint8_t* p, uint16_t value) {
    p[0] = uint8_t(value); p[1] = uint8_t(value >> 8U);
}
void write32(uint8_t* p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(value >> (8U*i));
}
bool valid(CalibrationRecordType type, const uint8_t* data, size_t size, size_t payload_size) {
    return data && type.data_type != 0U && payload_size > 0U &&
        payload_size <= CalibrationRecord::maximum_payload_size &&
        size == CalibrationRecord::header_size + payload_size &&
        read32(data) == 0x4D43414CU && read16(data+4) == type.version &&
        read16(data+6) == CalibrationRecord::header_size && read32(data+8) == type.data_type &&
        read32(data+12) == payload_size &&
        read32(data+16) == CalibrationRecord::crc32(data+CalibrationRecord::header_size, payload_size);
}
}

uint32_t CalibrationRecord::crc32(const void* bytes, size_t count)
{
    if (!bytes && count) return 0U;
    const auto* data = static_cast<const uint8_t*>(bytes);
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < count; ++i) {
        crc ^= data[i];
        for (unsigned bit = 0; bit < 8; ++bit)
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
    }
    return crc ^ 0xFFFFFFFFU;
}

size_t CalibrationRecord::encode(CalibrationRecordType type, uint32_t sequence,
                                  const void* payload, size_t payload_size,
                                  uint8_t* destination, size_t capacity)
{
    if (!payload || !destination || !type.data_type || !payload_size ||
        payload_size > maximum_payload_size || capacity < header_size + payload_size) return 0U;
    const uint32_t checksum = crc32(payload, payload_size);
    std::memmove(destination+header_size, payload, payload_size);
    write32(destination, 0x4D43414CU);
    write16(destination+4, type.version);
    write16(destination+6, header_size);
    write32(destination+8, type.data_type);
    write32(destination+12, static_cast<uint32_t>(payload_size));
    write32(destination+16, checksum);
    write32(destination+20, sequence);
    write32(destination+24, 0U);
    return header_size + payload_size;
}

bool CalibrationRecord::decode(CalibrationRecordType expected, const uint8_t* record, size_t size,
                               void* payload, size_t payload_size, uint32_t* sequence)
{
    if (!payload || !valid(expected, record, size, payload_size)) return false;
    const uint32_t stored_sequence = read32(record+20);
    std::memmove(payload, record+header_size, payload_size);
    if (sequence) *sequence = stored_sequence;
    return true;
}

bool CalibrationRecord::verify_readback(CalibrationRecordType expected,
                                        const uint8_t* record, size_t size,
                                        const void* payload, size_t payload_size)
{
    return payload && valid(expected, record, size, payload_size) &&
           std::memcmp(record+header_size, payload, payload_size) == 0;
}

}  // namespace algo::motor
