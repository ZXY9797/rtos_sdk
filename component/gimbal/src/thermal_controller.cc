#include <gimbal/thermal_controller.h>

#include <gimbal/math.h>

#include <cmath>

namespace gimbal {
namespace {

constexpr float kMinimumTemperatureC = -60.0F;
constexpr float kMaximumTemperatureC = 150.0F;
constexpr float kHighAmbientRateCPerS = 0.1F;
constexpr float kMaximumSamplePeriodS = 1.0F;

[[nodiscard]] bool finite_range(float value, float minimum, float maximum)
{
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

} // namespace

bool valid_thermal_config(const ThermalConfig &config)
{
    return finite_range(config.target_temperature_c,
                        kMinimumTemperatureC, kMaximumTemperatureC)
        && std::isfinite(config.stable_tolerance_c)
        && config.stable_tolerance_c > 0.0F
        && std::isfinite(config.maximum_temperature_c)
        && config.maximum_temperature_c
               > config.target_temperature_c
        && config.maximum_temperature_c <= kMaximumTemperatureC
        && std::isfinite(config.proportional_gain)
        && config.proportional_gain >= 0.0F
        && std::isfinite(config.integral_gain)
        && config.integral_gain >= 0.0F
        && std::isfinite(config.maximum_duty)
        && config.maximum_duty > 0.0F
        && config.maximum_duty <= 1.0F
        && std::isfinite(config.stable_time_s)
        && config.stable_time_s > 0.0F
        && std::isfinite(config.warmup_timeout_s)
        && config.warmup_timeout_s > config.stable_time_s
        && std::isfinite(config.rise_check_time_s)
        && config.rise_check_time_s > 0.0F
        && std::isfinite(config.minimum_rise_c)
        && config.minimum_rise_c > 0.0F;
}

bool ImuThermalController::configure(const ThermalConfig &config)
{
    if (!valid_thermal_config(config)) {
        is_configured_ = false;
        return false;
    }
    config_ = config;
    is_configured_ = true;
    stop();
    return true;
}

void ImuThermalController::start(float initial_temperature_c)
{
    output_ = {};
    if (!is_configured_
        || !finite_range(initial_temperature_c,
                         kMinimumTemperatureC, kMaximumTemperatureC)) {
        output_.mode = ThermalMode::Fault;
        output_.fault = ThermalFault::InvalidTemperature;
        return;
    }
    output_.mode = ThermalMode::Warmup;
    rise_check_start_c_ = initial_temperature_c;
    previous_temperature_c_ = initial_temperature_c;
}

void ImuThermalController::stop()
{
    output_ = {};
    integral_ = 0.0F;
    elapsed_s_ = 0.0F;
    stable_elapsed_s_ = 0.0F;
    rise_check_elapsed_s_ = 0.0F;
    rise_check_start_c_ = 0.0F;
    previous_temperature_c_ = 0.0F;
}

void ImuThermalController::clear_fault()
{
    if (output_.mode == ThermalMode::Fault) {
        stop();
    }
}

ThermalOutput ImuThermalController::update(float temperature_c,
                                            float sample_period_s)
{
    if (!is_configured_ || output_.mode == ThermalMode::Off
        || output_.mode == ThermalMode::Fault) {
        return output_;
    }
    if (!finite_range(temperature_c,
                      kMinimumTemperatureC, kMaximumTemperatureC)
        || !std::isfinite(sample_period_s) || sample_period_s <= 0.0F
        || sample_period_s > kMaximumSamplePeriodS) {
        return set_fault(ThermalFault::InvalidTemperature);
    }
    elapsed_s_ += sample_period_s;
    if (temperature_c >= config_.maximum_temperature_c) {
        return set_fault(ThermalFault::OverTemperature);
    }
    if (elapsed_s_ >= config_.warmup_timeout_s
        && temperature_c
               < config_.target_temperature_c
                    - config_.stable_tolerance_c) {
        return set_fault(ThermalFault::WarmupTimeout);
    }

    const float temperature_rate =
        (temperature_c - previous_temperature_c_) / sample_period_s;
    previous_temperature_c_ = temperature_c;
    const float error = config_.target_temperature_c - temperature_c;
    const bool high_ambient =
        error < -config_.stable_tolerance_c;
    if (high_ambient) {
        return update_high_ambient(temperature_rate, sample_period_s);
    }
    update_regulation(temperature_c, error, sample_period_s);
    update_rise_diagnostics(temperature_c, error, sample_period_s);
    return output_;
}

ThermalOutput ImuThermalController::set_fault(ThermalFault fault)
{
    output_.mode = ThermalMode::Fault;
    output_.fault = fault;
    output_.heater_duty = 0.0F;
    output_.is_ready = false;
    return output_;
}

ThermalOutput ImuThermalController::update_high_ambient(
    float temperature_rate_c_s, float sample_period_s)
{
    integral_ = 0.0F;
    output_.heater_duty = 0.0F;
    if (std::abs(temperature_rate_c_s) <= kHighAmbientRateCPerS) {
        stable_elapsed_s_ += sample_period_s;
    } else {
        stable_elapsed_s_ = 0.0F;
    }
    output_.is_ready = stable_elapsed_s_ >= config_.stable_time_s;
    output_.mode = output_.is_ready
        ? ThermalMode::Stable : ThermalMode::Stabilizing;
    return output_;
}

void ImuThermalController::update_regulation(
    float temperature_c, float error_c, float sample_period_s)
{
    integral_ = math::clamp(
        integral_ + config_.integral_gain * error_c * sample_period_s,
        0.0F, config_.maximum_duty);
    const float command = config_.proportional_gain * error_c + integral_;
    output_.heater_duty = math::clamp(
        command, 0.0F, config_.maximum_duty);
    const bool within_tolerance =
        std::abs(error_c) <= config_.stable_tolerance_c;
    if (within_tolerance) {
        stable_elapsed_s_ += sample_period_s;
    } else {
        stable_elapsed_s_ = 0.0F;
    }
    if (stable_elapsed_s_ >= config_.stable_time_s) {
        output_.mode = ThermalMode::Stable;
        output_.is_ready = true;
    } else if (temperature_c >= config_.target_temperature_c
                                      - config_.stable_tolerance_c) {
        output_.mode = ThermalMode::Stabilizing;
    } else {
        output_.mode = ThermalMode::Warmup;
    }
}

void ImuThermalController::update_rise_diagnostics(
    float temperature_c, float error_c, float sample_period_s)
{
    const bool rise_expected =
        output_.heater_duty >= 0.5F * config_.maximum_duty
        && error_c > config_.stable_tolerance_c;
    if (!rise_expected) {
        rise_check_elapsed_s_ = 0.0F;
        rise_check_start_c_ = temperature_c;
        return;
    }
    rise_check_elapsed_s_ += sample_period_s;
    if (rise_check_elapsed_s_ < config_.rise_check_time_s) {
        return;
    }
    if (temperature_c - rise_check_start_c_ < config_.minimum_rise_c) {
        (void)set_fault(ThermalFault::NoTemperatureRise);
    } else {
        rise_check_elapsed_s_ = 0.0F;
        rise_check_start_c_ = temperature_c;
    }
}

} // namespace gimbal
