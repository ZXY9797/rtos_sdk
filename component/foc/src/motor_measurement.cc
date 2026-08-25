#include <foc/motor_measurement.h>
#include <foc/math_utils.h>
#include <cmath>

namespace foc {

void MotorMeasurement::start() {
    count_ = 0;
    accum_ = 0.0f;
    accum2_ = 0.0f;
    phase_ = 0.0f;
    state_.store(State::MeasuringRs, std::memory_order_release);
}

bool MotorMeasurement::update(float vbus, float dt, Vec2 &v_ab_out) {
    switch (state_.load(std::memory_order_acquire)) {
    case State::MeasuringRs: {
        // 施加直流电压测量 Rs
        float v_test = test_current_ * 0.05f; // 初始猜测 Rs=50mΩ
        v_ab_out = {v_test, 0.0f};

        count_++;
        accum_ += v_test;

        if (count_ >= 1000) {
            // 计算 Rs = V / I
            float v_avg = accum_ / static_cast<float>(count_);
            rs_.store(v_avg / test_current_, std::memory_order_relaxed);
            state_.store(State::MeasuringLs, std::memory_order_release);
            count_ = 0;
            accum_ = 0.0f;
            phase_ = 0.0f;
        }
        return true;
    }

    case State::MeasuringLs: {
        // 施加高频正弦电压测量 Ls
        phase_ += 1000.0f * dt; // 1kHz 测试频率
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        float v_test = test_current_ * 0.1f * sinf(phase_ * 2.0f * 3.14159265f);
        v_ab_out = {v_test, 0.0f};

        count_++;
        accum_ += v_test * v_test;

        if (count_ >= 2000) {
            // Ls = V_rms / (2π * f * I_rms)
            float v_rms = sqrtf(accum_ / static_cast<float>(count_));
            float i_rms = test_current_;
            const float inductance =
                v_rms / (2.0f * 3.14159265f * 1000.0f * i_rms);
            ld_.store(inductance, std::memory_order_relaxed);
            lq_.store(inductance, std::memory_order_relaxed);
            state_.store(State::MeasuringFlux, std::memory_order_release);
            count_ = 0;
            accum_ = 0.0f;
        }
        return true;
    }

    case State::MeasuringFlux: {
        // 开环旋转测量磁链
        phase_ += 10.0f * dt; // 10Hz 旋转
        if (phase_ >= 1.0f) phase_ -= 1.0f;

        float angle = phase_ * 65536.0f;
        const float v_test = test_current_
            * rs_.load(std::memory_order_relaxed) * 1.5f;
        float s, c;
        sin_cos(static_cast<uint16_t>(angle), s, c);
        v_ab_out = {v_test * c, v_test * s};

        count_++;
        if (count_ >= 5000) {
            // 磁链估算
            flux_.store(
                vbus / (2.0f * 3.14159265f * 10.0f
                        * static_cast<float>(pole_pairs_)),
                std::memory_order_relaxed);
            state_.store(State::Done, std::memory_order_release);
        }
        return true;
    }

    default:
        v_ab_out = {0.0f, 0.0f};
        return false;
    }
}

} // namespace foc
