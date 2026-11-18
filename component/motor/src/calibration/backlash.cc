#include <algo/calibration/backlash.h>
#include <cmath>
#include <initializer_list>

namespace algo::motor {

bool BacklashIdentifier::configure(float frequency_hz, const Config& config)
{
    if (active() || !std::isfinite(frequency_hz) || frequency_hz <= 0.0F ||
        config.cycles_per_speed == 0U || !std::isfinite(config.cruise_travel_rad) ||
        config.cruise_travel_rad <= 0.0F) return false;
    for (float duration : {config.ramp_time_s, config.cruise_settle_time_s,
                           config.stopped_wait_time_s, config.total_timeout_s})
        if (!std::isfinite(duration) || duration * frequency_hz < 1.0F ||
            duration * frequency_hz >= 4294967040.0F) return false;
    for (float speed : config.speeds_rad_s)
        if (!std::isfinite(speed) || speed <= 0.0F) return false;
    frequency_hz_ = frequency_hz;
    m_backlash_test = {};
    m_backlash_test.config = config;
    configured_ = true;
    return true;
}

bool BacklashIdentifier::start(bool control_disabled)
{
    if (!configured_ || active() || !control_disabled) return false;
    m_backlash_test.result = {};
    initialize_backlash_test();
    return true;
}

void BacklashIdentifier::cancel(bool emergency)
{
    if (emergency) m_backlash_test.request.emergency_stop_requested = true;
    else m_backlash_test.request.stop_requested = true;
}

float BacklashIdentifier::update(float input_equivalent_angle_rad, float output_angle_rad,
                                 bool protection_active)
{
    if (!active()) return 0.0F;
    if (protection_active || !std::isfinite(input_equivalent_angle_rad) ||
        !std::isfinite(output_angle_rad)) {
        finish_backlash_test(static_cast<uint8_t>(MotorStatus::HardwareError), false);
        return 0.0F;
    }
    input_angle_rad_ = input_equivalent_angle_rad;
    output_angle_rad_ = output_angle_rad;
    run_backlash_test_step();
    return active() ? m_backlash_test.runtime.command_velocity_rad_s : 0.0F;
}

void BacklashIdentifier::initialize_backlash_test()
{
    auto& runtime = m_backlash_test.runtime;
    runtime = {};
    runtime.active = true;
    runtime.direction = 1;
    begin_backlash_phase(BacklashTestState::RampUp);
}

void BacklashIdentifier::begin_backlash_phase(BacklashTestState state)
{
    auto& runtime = m_backlash_test.runtime;
    runtime.state = state;
    runtime.phase_count = 0U;
    if (state == BacklashTestState::RampUp) {
        runtime.command_velocity_rad_s = 0.0F;
        runtime.ramp_start_velocity_rad_s = 0.0F;
    } else if (state == BacklashTestState::Cruise) {
        runtime.branch_sampling_started = false;
        runtime.branch_start_input_angle_rad = 0.0F;
        runtime.branch_error_sum_rad = 0.0F;
        runtime.branch_error_compensation_rad = 0.0F;
        runtime.branch_sample_count = 0U;
    }
}

void BacklashIdentifier::finish_backlash_test(uint8_t ret, bool result_valid)
{
    auto& runtime = m_backlash_test.runtime;
    const auto& config = m_backlash_test.config;
    const bool result_was_requested = result_valid;
    const uint8_t completed_cycles = static_cast<uint8_t>(
        (runtime.speed_index * config.cycles_per_speed) + runtime.cycle_index);
    float result_rad = 0.0F;
    if (result_valid && (runtime.valid_cycles != 0U)) {
        result_rad = runtime.backlash_sum_rad /
                     static_cast<float>(runtime.valid_cycles);
        result_valid = algo::motor::basic_math::is_finite_float(result_rad);
    } else {
        result_valid = false;
    }
    if (result_was_requested && !result_valid && (ret == 0U)) {
        ret = static_cast<uint8_t>(MotorStatus::HardwareError);
    }

    BacklashTestResultSnapshot snapshot {};
    snapshot.active = false;
    snapshot.finished = true;
    snapshot.result_valid = result_valid;
    snapshot.ret = ret;
    snapshot.state = BacklashTestState::Idle;
    snapshot.speed_index = runtime.speed_index;
    snapshot.completed_cycles = completed_cycles;
    snapshot.valid_cycles = runtime.valid_cycles;
    snapshot.result_rad = result_valid ? result_rad : 0.0F;

    runtime.active = false;
    runtime.state = BacklashTestState::Idle;
    runtime.command_velocity_rad_s = 0.0F;
    m_backlash_test.request = {};
    m_backlash_test.result = snapshot;
}

bool BacklashIdentifier::update_backlash_result_progress()
{
    auto& runtime = m_backlash_test.runtime;
    const auto& config = m_backlash_test.config;
    if (!runtime.active) {
        return false;
    }
    const uint32_t total_timeout_count = static_cast<uint32_t>(
        config.total_timeout_s * frequency_hz_);
    if (runtime.total_count >= total_timeout_count) {
        finish_backlash_test(static_cast<uint8_t>(MotorStatus::Timeout),
                             false);
        return false;
    }


    auto& result = m_backlash_test.result;
    result.active = true;
    result.finished = false;
    result.result_valid = false;
    result.ret = 0U;
    result.state = runtime.state;
    result.speed_index = runtime.speed_index;
    result.completed_cycles = static_cast<uint8_t>(
        (runtime.speed_index * config.cycles_per_speed) +
        runtime.cycle_index);
    result.valid_cycles = runtime.valid_cycles;
    result.result_rad = 0.0F;
    return true;
}

void BacklashIdentifier::run_backlash_test_step()
{
    BacklashTest::Request request {};
    {

        request = m_backlash_test.request;
        m_backlash_test.request = {};
    }

    if (request.start_requested) {
        initialize_backlash_test();
    }
    auto& runtime = m_backlash_test.runtime;
    const auto& config = m_backlash_test.config;
    if (!runtime.active) {
        return;
    }
    if (request.emergency_stop_requested) {
        finish_backlash_test(kBacklashTestCancelledRet, false);
        return;
    }

    if (request.stop_requested) {
        runtime.cancel_requested = true;
        if ((runtime.state == BacklashTestState::RampUp) ||
            (runtime.state == BacklashTestState::Cruise)) {
            runtime.ramp_start_velocity_rad_s =
                runtime.command_velocity_rad_s;
            begin_backlash_phase(BacklashTestState::RampDown);
        }
    }

    ++runtime.total_count;
    if (!update_backlash_result_progress()) {
        return;
    }

    const float frequency_hz = frequency_hz_;
    const uint32_t ramp_count = static_cast<uint32_t>(
        config.ramp_time_s * frequency_hz);
    const uint32_t settle_count = static_cast<uint32_t>(
        config.cruise_settle_time_s * frequency_hz);
    const uint32_t stop_wait_count = static_cast<uint32_t>(
        config.stopped_wait_time_s * frequency_hz);
    const float target_velocity_rad_s =
        static_cast<float>(runtime.direction) *
        config.speeds_rad_s[runtime.speed_index];

    switch (runtime.state) {
    case BacklashTestState::RampUp: {
        const float progress =
            static_cast<float>(runtime.phase_count + 1U) /
            static_cast<float>(ramp_count);
        runtime.command_velocity_rad_s =
            target_velocity_rad_s *
            algo::motor::trajectory::smoothstep(progress);
        ++runtime.phase_count;
        if (runtime.phase_count >= ramp_count) {
            runtime.command_velocity_rad_s = target_velocity_rad_s;
            begin_backlash_phase(BacklashTestState::Cruise);
        }
        break;
    }
    case BacklashTestState::Cruise: {
        runtime.command_velocity_rad_s = target_velocity_rad_s;
        ++runtime.phase_count;
        // RampUp 结束后继续用恒定目标速度稳定一段时间；斜坡和稳定等待
        // 均不进入支路均值，正式采样窗口严格覆盖一整圈恒速运动。
        if (runtime.phase_count <= settle_count) {
            break;
        }

        const float input_angle_rad =
            input_angle_rad_;
        const float output_angle_rad =
            output_angle_rad_;
        if (!runtime.branch_sampling_started) {
            runtime.branch_start_input_angle_rad = input_angle_rad;
            runtime.branch_sampling_started = true;
        }

        const float directed_travel_rad =
            static_cast<float>(runtime.direction) *
            (input_angle_rad - runtime.branch_start_input_angle_rad);
        const float transmission_error_rad = input_angle_rad - output_angle_rad;
        if (!algo::motor::basic_math::is_finite_float(directed_travel_rad) ||
            !algo::motor::basic_math::is_finite_float(transmission_error_rad)) {
            finish_backlash_test(
                static_cast<uint8_t>(MotorStatus::HardwareError), false);
            return;
        }

        // 整圈样本数较大，用 Kahan 补偿降低长时间 float 累加误差。
        const float compensated_error_rad =
            transmission_error_rad - runtime.branch_error_compensation_rad;
        const float updated_error_sum_rad =
            runtime.branch_error_sum_rad + compensated_error_rad;
        runtime.branch_error_compensation_rad =
            (updated_error_sum_rad - runtime.branch_error_sum_rad) -
            compensated_error_rad;
        runtime.branch_error_sum_rad = updated_error_sum_rad;
        ++runtime.branch_sample_count;
        if (directed_travel_rad >= config.cruise_travel_rad) {
            runtime.branch_mean_rad =
                runtime.branch_error_sum_rad /
                static_cast<float>(runtime.branch_sample_count);
            if (!algo::motor::basic_math::is_finite_float(
                    runtime.branch_mean_rad)) {
                finish_backlash_test(
                    static_cast<uint8_t>(MotorStatus::HardwareError), false);
                return;
            }
            runtime.ramp_start_velocity_rad_s =
                runtime.command_velocity_rad_s;
            begin_backlash_phase(BacklashTestState::RampDown);
        }
        break;
    }
    case BacklashTestState::RampDown: {
        const float progress =
            static_cast<float>(runtime.phase_count + 1U) /
            static_cast<float>(ramp_count);
        runtime.command_velocity_rad_s =
            runtime.ramp_start_velocity_rad_s *
            (1.0F - algo::motor::trajectory::smoothstep(progress));
        ++runtime.phase_count;
        if (runtime.phase_count >= ramp_count) {
            runtime.command_velocity_rad_s = 0.0F;
            begin_backlash_phase(BacklashTestState::WaitStopped);
        }
        break;
    }
    case BacklashTestState::WaitStopped: {
        runtime.command_velocity_rad_s = 0.0F;
        ++runtime.phase_count;
        // 速度已用平滑斜坡降为零；固定等待机械端停稳，避免输出 TMR
        // 静止噪声让判停条件永远无法连续成立。
        if (runtime.phase_count < stop_wait_count) {
            break;
        }

        if (runtime.cancel_requested) {
            finish_backlash_test(kBacklashTestCancelledRet, false);
            return;
        }
        if (runtime.direction > 0) {
            runtime.positive_branch_mean_rad = runtime.branch_mean_rad;
            runtime.direction = -1;
            begin_backlash_phase(BacklashTestState::RampUp);
            break;
        }

        const float cycle_backlash_rad = fabsf(
            runtime.positive_branch_mean_rad - runtime.branch_mean_rad);
        if (!algo::motor::basic_math::is_finite_float(cycle_backlash_rad)) {
            finish_backlash_test(
                static_cast<uint8_t>(MotorStatus::HardwareError), false);
            return;
        }
        runtime.backlash_sum_rad += cycle_backlash_rad;
        ++runtime.valid_cycles;
        ++runtime.cycle_index;
        runtime.direction = 1;
        if (runtime.cycle_index >= config.cycles_per_speed) {
            runtime.cycle_index = 0U;
            ++runtime.speed_index;
        }
        if (runtime.speed_index >= config.speeds_rad_s.size()) {
            finish_backlash_test(0U, true);
            return;
        }
        begin_backlash_phase(BacklashTestState::RampUp);
        break;
    }
    case BacklashTestState::Idle:
    default:
        finish_backlash_test(
            static_cast<uint8_t>(MotorStatus::HardwareError), false);
        return;
    }

}

}  // namespace algo::motor
