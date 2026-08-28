#include <imu/icm40609d.h>

#include <cstdint>

#define CHECK(condition) do { if (!(condition)) { return __LINE__; } } while (0)

namespace {

using TestImu = imu::Icm40609d<0, 0x40000000U, 0U, 0U>;

int test_decode_preserves_wire_order_and_sign()
{
    imu::Icm40609dRawFrame frame {};
    const uint8_t wire[imu::kIcm40609dWireBytes] {
        0x00U,
        0x12U, 0x34U,
        0xFFU, 0xFEU,
        0x00U, 0x03U,
        0x01U, 0x02U,
        0x80U, 0x00U,
        0x7FU, 0xFFU,
        0xABU, 0xCDU,
    };
    for (size_t index = 0U; index < sizeof(wire); ++index) {
        frame.wire[index] = wire[index];
    }

    imu::ImuData data {};
    CHECK(TestImu::decode(frame, data));
    CHECK(data.accel[0] == 0x1234);
    CHECK(data.accel[1] == -2);
    CHECK(data.accel[2] == 3);
    CHECK(data.temp == 0x0102);
    CHECK(data.gyro[0] == static_cast<int16_t>(0x8000U));
    CHECK(data.gyro[1] == 0x7FFF);
    CHECK(data.gyro[2] == static_cast<int16_t>(0xABCDU));
    return 0;
}

} // namespace

int main()
{
    return test_decode_preserves_wire_order_and_sign();
}
