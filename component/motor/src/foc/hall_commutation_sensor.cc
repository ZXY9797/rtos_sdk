#include <foc/hall_sensor.h>
#include <foc/math_utils.h>

namespace foc {

// Hall 状态 → 电角度查找表 (60° 步进)
static const uint16_t HALL_ANGLE_TABLE[8] = {
    0,      // 000: 无效
    0x0000, // 001: 0°
    0x5555, // 010: 120°
    0x2AAA, // 011: 60°
    0xAAAA, // 100: 240°
    0xD555, // 101: 300°
    0x7FFF, // 110: 180°
    0,      // 111: 无效
};

void HallSensor::update(uint8_t hall_state) {
    if (hall_state > 7 || hall_state == 0 || hall_state == 7) return;

    uint16_t new_angle = HALL_ANGLE_TABLE[hall_state];
    if (!forward_) {
        new_angle = -new_angle; // 反转方向
    }

    // 计算速度
    if (hall_state_ != 0 && hall_state_ != 7) {
        int32_t delta = static_cast<int32_t>(new_angle) - static_cast<int32_t>(prev_angle_);
        // 处理环绕
        if (delta > 32767) delta -= 65536;
        if (delta < -32768) delta += 65536;

        if (interp_period_ > 0) {
            speed_ = static_cast<float>(delta) / static_cast<float>(interp_period_) * 65536.0f;
        }
    }

    prev_angle_ = angle_;
    angle_ = new_angle;
    hall_state_ = hall_state;
    interp_count_ = 0;
}

} // namespace foc
