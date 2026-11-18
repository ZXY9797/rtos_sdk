#include <algo/calibration/calibration.h>
#include <algo/foc/current_loop.h>
#include <algo/motor/joint_controller.h>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace algo::motor;

static void near(float actual, float expected, float tolerance = 1.0e-4F)
{
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::fabs(actual - expected) > tolerance) {
        std::cerr << actual << " != " << expected << '\n';
        std::abort();
    }
}

static void test_transforms_and_pwm()
{
    const foc::Phase3 phases {1.0F, -0.5F, -0.5F};
    const auto ab = foc::clarke_transform(phases);
    near(ab.alpha, 1.0F);
    near(ab.beta, 0.0F);
    for (unsigned i = 0; i < 360; ++i) {
        const float angle = float(i) * basic_math::kTwoPi / 360.0F;
        const auto dq = foc::park_transform(ab, angle);
        const auto restored = foc::inverse_park_transform(dq, angle);
        near(restored.alpha, ab.alpha);
        near(restored.beta, ab.beta);
        const auto sector = foc::svpwm_get_sector(restored);
        const auto times = foc::svpwm_calculate_times(restored, sector, 24.0F, 1.0F);
        assert(times.duty_u >= 0 && times.duty_u <= 100);
    }
    CurrentLoop loop;
    CurrentLoopConfig config;
    config.d_axis = config.q_axis = {0.7F, 400.0F, 0, 0, 20000.0F, 15.0F, 15.0F};
    assert(loop.configure(config));
    CurrentLoopInput input;
    input.bus_voltage_v = 24.0F;
    input.mode = CurrentLoopMode::Current;
    input.target_current_a.q = 1.0F;
    auto output = loop.update(input);
    assert(output.valid && output.energize);
    near(output.voltage_v.q, 0.7F * (1 + basic_math::kTwoPi * 400.0F / 20000.0F));
    const float held_voltage = output.voltage_v.q;
    input.mode = CurrentLoopMode::HeldQVoltage;
    input.target_current_a.q = 100;
    output = loop.update(input);
    near(output.voltage_v.q, held_voltage);
    near(output.voltage_v.d, 0.0F);
    input.mode = CurrentLoopMode::Disabled;
    output = loop.update(input);
    assert(output.valid && !output.energize);
    near(output.duty_percent.u, 50.0F);
    input.mode = CurrentLoopMode::OpenLoop;
    input.target_open_loop_angle_rad = 10;
    input.voltage_v.d = 2;
    output = loop.update(input);
    near(output.applied_electrical_angle_rad, 0.02F);
    input.bus_voltage_v = 0;
    assert(!loop.update(input).energize);
    input.bus_voltage_v = 24;
    input.mode = CurrentLoopMode::DirectAlphaBeta;
    input.direct_voltage_v = {std::numeric_limits<float>::infinity(), 0.0F};
    assert(!loop.update(input).energize);
    input.mode = CurrentLoopMode::Current;
    input.current_a = {std::numeric_limits<float>::max(),
                      -std::numeric_limits<float>::max(),
                      -std::numeric_limits<float>::max()};
    assert(!loop.update(input).energize);
    assert(CurrentLoop::duty_count(50, 1001) == 500U);
}

static void test_feedback()
{
    MotorElecCalibData raw, calibration;
    raw.iu_calib = {true, 1.65F, -20.0F};
    calibration.iu_calib = {true, 0.1F, 1.2F};
    MotorElecInfoFeedback feedback;
    assert(current_ctrl_feedback_init(raw, calibration, &feedback, nullptr, 0.9F));
    assert(update_elec_info(1.7F, 0, 0, 24, &feedback));
    near(feedback.iu_raw, -1.0F);
    near(feedback.iu_filter, -1.1F);
    assert(update_elec_info(1.65F, 0, 0, 24, &feedback));
    near(feedback.iu_filter, -0.02F);
}

static void test_calibration()
{
    CalibrationConfig config;
    config.motor.pole_pairs = 14;
    config.motor.gear_ratio = 48;
    config.input_tmr = {0, 10};
    config.electrical_angle = {0, 100};
    config.output_tmr = {0, 2};
    Calibrator calibration;
    assert(calibration.configure(config));
    MotorSensorCalibData original;
    assert(!calibration.start(CalibrationKind::ElectricalAngle, 0, original, true));
    assert(calibration.start(CalibrationKind::InputTmr, 0, original, true));
    for (unsigned i = 0; i < 448; ++i) {
        const float angle = float(i) * basic_math::kTwoPi / 448.0F;
        CalibrationSample sample;
        sample.valid = true;
        sample.rotor_tmr1 = 1.6F + 0.8F * std::sin(angle);
        sample.rotor_tmr2 = 1.7F + 0.6F * std::cos(angle) + 0.2F * std::sin(angle);
        calibration.update((i + 1) * 10U, sample);
    }
    assert(calibration.state() == CalibrationState::PendingPersistence);
    assert(!calibration.drive().energize);
    near(calibration.result().input_sensor_calib.center[0], 1.6F);
    near(calibration.result().input_sensor_calib.center[1], 1.7F);
    const auto& affine = calibration.result().input_sensor_calib;
    for (unsigned i = 0; i < 360; ++i) {
        const float angle = float(i) * basic_math::kTwoPi / 360.0F;
        const float x = 0.8F * std::sin(angle);
        const float y = 0.6F * std::cos(angle) + 0.2F * std::sin(angle);
        const float fitted_x = affine.transform[0] * x + affine.transform[1] * y;
        const float fitted_y = affine.transform[2] * x + affine.transform[3] * y;
        near(fitted_x * fitted_x + fitted_y * fitted_y, 1.0F, 5.0e-4F);
    }
    assert(valid_linear_calibration({true, 0.0F, -1.0F}));
    assert(!valid_linear_calibration({true, 0.0F, 1.0e-7F}));
    assert(calibration.complete_persistence(true));
    assert(calibration.result().input_sensor_calib.valid);
    const auto fitted = calibration.result();
    assert(calibration.start(CalibrationKind::ElectricalAngle, 0xFFFFFF00U, fitted, true));
    calibration.update(0xFFFFFF00U + calibration.timeout_ms() + 1U, {});
    assert(calibration.state() == CalibrationState::Timeout);
    assert(calibration.result().input_sensor_calib.valid);
    assert(!calibration.drive().energize);
}

int main()
{
    test_transforms_and_pwm();
    test_feedback();
    test_calibration();
    std::cout << "motor algorithm contracts passed\n";
}
