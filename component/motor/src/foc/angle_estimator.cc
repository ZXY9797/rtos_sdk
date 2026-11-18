#include <foc/angle_estimator.h>
#include <foc/math_utils.h>

namespace foc {

void AngleEstimator::update(uint16_t sensor_angle, float speed, float dt) {
    if (open_loop_) {
        open_loop_accum_ += open_loop_speed_ * dt;
        if (open_loop_accum_ >= 65536.0f) open_loop_accum_ -= 65536.0f;
        if (open_loop_accum_ < 0.0f) open_loop_accum_ += 65536.0f;
        angle_ = static_cast<uint16_t>(static_cast<int32_t>(open_loop_accum_) + offset_);
    } else {
        // 积分速度得到角度
        accum_ += speed * dt * 65536.0f;
        if (accum_ >= 65536.0f) accum_ -= 65536.0f;
        if (accum_ < 0.0f) accum_ += 65536.0f;

        // 如果有传感器，混合
        if (sensor_angle != 0) {
            angle_ = sensor_angle + static_cast<uint16_t>(offset_);
        } else {
            angle_ = static_cast<uint16_t>(static_cast<int32_t>(accum_) + offset_);
        }
    }
}

SinCos AngleEstimator::sin_cos() const {
    SinCos sc;
    foc::sin_cos(angle_, sc.sin, sc.cos);
    return sc;
}

void AngleEstimator::set_open_loop(bool enable, float speed_hz) {
    open_loop_ = enable;
    open_loop_speed_ = speed_hz;
    if (enable) {
        open_loop_accum_ = 0.0f;
    }
}

} // namespace foc
