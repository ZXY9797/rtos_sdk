#include "link/frame.h"

#include <array>

namespace link {
namespace {

constexpr std::array<uint16_t, 256U> make_crc16_table()
{
    std::array<uint16_t, 256U> table {};
    for (size_t index = 0U; index < table.size(); ++index) {
        uint16_t crc = static_cast<uint16_t>(index << 8U);
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = static_cast<uint16_t>(
                (crc << 1U) ^ ((crc & 0x8000U) != 0U ? 0x1021U : 0U));
        }
        table[index] = crc;
    }
    return table;
}

constexpr auto crc16_table = make_crc16_table();

} // namespace

uint16_t crc16_ccitt(const uint8_t* data, size_t len)
{
    uint16_t crc = 0xFFFFU;
    for (size_t index = 0U; index < len; ++index) {
        const uint8_t table_index = static_cast<uint8_t>((crc >> 8U) ^ data[index]);
        crc = static_cast<uint16_t>((crc << 8U) ^ crc16_table[table_index]);
    }
    return crc;
}

} // namespace link
