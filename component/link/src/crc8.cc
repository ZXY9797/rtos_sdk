#include "link/frame.h"

#include <array>

namespace link {
namespace {

constexpr std::array<uint8_t, 256U> make_crc8_table()
{
    std::array<uint8_t, 256U> table {};
    for (size_t index = 0U; index < table.size(); ++index) {
        uint8_t crc = static_cast<uint8_t>(index);
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = static_cast<uint8_t>(
                (crc << 1U) ^ ((crc & 0x80U) != 0U ? 0x07U : 0U));
        }
        table[index] = crc;
    }
    return table;
}

constexpr auto crc8_table = make_crc8_table();

} // namespace

uint8_t crc8_smbus(const uint8_t* data, size_t len)
{
    uint8_t crc = 0U;
    for (size_t index = 0U; index < len; ++index) {
        crc = crc8_table[crc ^ data[index]];
    }
    return crc;
}

} // namespace link
