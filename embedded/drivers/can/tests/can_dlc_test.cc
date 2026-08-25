#include <drivers/can_dlc.h>

#include <array>
#include <cstdint>

#define CHECK(condition) do { if (!(condition)) { return __LINE__; } } while (0)

int main()
{
    constexpr std::array<uint8_t, 16U> lengths = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U,
        8U, 12U, 16U, 20U, 24U, 32U, 48U, 64U,
    };
    for (uint8_t dlc = 0U; dlc < lengths.size(); ++dlc) {
        uint8_t decoded = 0U;
        CHECK(hal::can_fd_dlc_to_length(dlc, decoded));
        CHECK(decoded == lengths[dlc]);

        uint8_t encoded = 0U;
        uint8_t padded = 0U;
        CHECK(hal::can_fd_length_to_dlc(decoded, encoded, padded));
        CHECK(encoded == dlc);
        CHECK(padded == decoded);
    }

    uint8_t dlc = 0U;
    uint8_t padded = 0U;
    CHECK(hal::can_fd_length_to_dlc(9U, dlc, padded));
    CHECK(dlc == 9U && padded == 12U);
    CHECK(hal::can_fd_length_to_dlc(33U, dlc, padded));
    CHECK(dlc == 14U && padded == 48U);
    CHECK(!hal::can_fd_length_to_dlc(65U, dlc, padded));
    CHECK(!hal::can_fd_dlc_to_length(16U, padded));
    return 0;
}
