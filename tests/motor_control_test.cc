#include <algo/motor/parameters.h>
#include <algo/motor/feedback_processor.h>
#include <cassert>
#include <cmath>
#include <limits>

using namespace algo::motor;

static void test_all_presets()
{
    const float gears[] {48.0F, 8.0F*6.0F/2.8F, 8.0F, 48.0F, 45.0F, 48.0F, 64.0F};
    for (unsigned i = 0; i < 7; ++i) {
        MotorParameters parameters;
        assert(motor_parameters(static_cast<MotorVariant>(i), parameters));
        assert(parameters.motor.pole_pairs == 14);
        assert(std::fabs(parameters.motor.gear_ratio - gears[i]) < 1.0e-5F);
        assert(parameters.current_frequency_hz == 20000.0F);
        assert(parameters.joint.frequency_hz == 4000.0F);
        assert(parameters.ntc_count == 35U);
        CurrentLoop current;
        JointController joint;
        Calibrator calibration;
        Protection protection;
        assert(current.configure(parameters.current));
        assert(joint.configure(parameters.joint));
        assert(calibration.configure(parameters.calibration));
        assert(protection.configure(parameters.temperature, parameters.bus));
        float temperature = 0;
        assert(Protection::temperature(1.65F, parameters.ntc_table, parameters.ntc_count, temperature));
        assert(std::fabs(temperature - 25.0F) < 1.0e-4F);
        const float voltages[] {1.65F, 1.65F, 3.3F};
        const auto temperatures = Protection::sample_temperatures(voltages, 3,
            parameters.ntc_table, parameters.ntc_count);
        assert(temperatures.valid && temperatures.present_mask == 7U);
        assert(temperatures.valid_mask == 3U);
        const float disconnected[] {1.65F, 3.3F, 3.3F};
        assert(!Protection::sample_temperatures(disconnected, 3,
            parameters.ntc_table, parameters.ntc_count).valid);
    }
    MotorParameters parameters;
    assert(!motor_parameters(static_cast<MotorVariant>(255), parameters));
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    assert(parameters.joint.loop.speed.q_current_limit_a == 2.2F);
    assert(parameters.calibration.input_voltage_v == 2.0F);
    assert(parameters.calibration.electrical_voltage_v == 3.0F);
    assert(parameters.joint.loop.position.velocity_feedforward_enable);
}

static void test_torque_mit_transition_and_disable()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    JointController joint;
    assert(joint.configure(parameters.joint));
    const JointLoopFeedback feedback {};
    assert(joint.select_mode(JointTargetMode::Torque, feedback));
    JointTargetOrder target;
    target.torque_a = 1.0F;
    target.host_timestamp_us = 1000;
    assert(joint.submit(target));
    auto output = joint.update(feedback, true);
    assert(output.valid && output.energize && output.q_current_a == 0.0F);
    float last = output.q_current_a;
    for (unsigned i = 0; i < 100; ++i) {
        output = joint.update(feedback, true);
        assert(output.q_current_a >= last && output.q_current_a <= 1.00001F);
        last = output.q_current_a;
    }
    assert(std::fabs(output.q_current_a - 1.0F) < 1.0e-5F);
    assert(joint.select_mode(JointTargetMode::Mit, feedback));
    target.torque_a = 0.5F;
    target.host_timestamp_us = 2000;
    assert(joint.submit(target));
    output = joint.update(feedback, true);
    assert(std::fabs(output.q_current_a - 1.0F) < 1.0e-5F);
    for (unsigned i = 0; i < 100; ++i) output = joint.update(feedback, true);
    assert(std::fabs(output.q_current_a - 0.5F) < 1.0e-5F);
    assert(!joint.update(feedback, false).energize);
    output = joint.update(feedback, true);
    assert(std::fabs(output.q_current_a) < 1.0e-5F);
    assert(!joint.update({std::numeric_limits<float>::quiet_NaN(), 0, 0}, true).energize);
}

static void test_speed_uses_rotor_feedback()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    JointController first, second;
    assert(first.configure(parameters.joint) && second.configure(parameters.joint));
    assert(first.select_mode(JointTargetMode::Velocity, {}));
    assert(second.select_mode(JointTargetMode::Velocity, {}));
    JointTargetOrder target;
    target.host_timestamp_us = 100;
    assert(first.submit(target) && second.submit(target));
    const auto output_first = first.update({0, 100, 0}, true);
    const auto output_second = second.update({0, 0, 100}, true);
    assert(output_first.valid && output_second.valid);
    assert(std::fabs(output_first.speed_observer_output_a) < 1.0e-6F);
    assert(std::fabs(output_second.speed_observer_output_a) > 0.1F);
}

static void test_s_curve_limits()
{
    trajectory::SCurvePlanner planner;
    trajectory::SCurvePlannerConfig config;
    config.default_max_velocity_rad_s = config.max_velocity_limit_rad_s = 2;
    config.max_acceleration_rad_s2 = 4;
    config.max_jerk_rad_s3 = 20;
    config.sample_time_s = 0.001F;
    assert(planner.configure(config));
    assert(planner.plan(1, 2));
    for (unsigned i = 0; i < 10000 && planner.active(); ++i) {
        const auto sample = planner.update();
        assert(std::fabs(sample.velocity_rad_s) <= 2.001F);
        assert(std::fabs(sample.acceleration_rad_s2) <= 4.001F);
    }
    assert(planner.finished());
    assert(std::fabs(planner.sample().position_rad - 1) < 1.0e-5F);
    assert(planner.sample().velocity_rad_s == 0);
}

static void test_position_enable_starts_at_feedback()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    JointController joint;
    assert(joint.configure(parameters.joint));
    JointTargetOrder target;
    target.position_rad = 6;
    target.velocity_rad_s = 1;
    target.torque_a = 2;
    target.host_timestamp_us = 100;
    assert(joint.submit(target));
    const auto output = joint.update({5, 0, 0}, true);
    assert(output.valid);
    assert(output.planned_position_rad >= 5 && output.planned_position_rad < 5.001F);
}

static void test_protection_and_adc_scaling()
{
    Protection protection;
    TemperatureProtectConfig temperature;
    temperature.trigger_count = 3;
    temperature.recover_count = 2;
    VbusProtectConfig bus;
    assert(protection.configure(temperature, bus));
    protection.update_temperature(100, 20, true);
    protection.update_temperature(100, 20, true);
    assert(!protection.active());
    protection.update_temperature(100, 20, true);
    assert(protection.fault_mask() == (1U << 2U));
    protection.update_temperature(20, 20, false);
    assert(protection.active());
    protection.update_temperature(50, 20, true);
    assert(protection.active());
    protection.update_temperature(50, 20, true);
    assert(!protection.active());
    protection.update_bus(46, true);
    assert(protection.fault_mask() == (1U << 4U));
    for (unsigned i = 0; i < 4; ++i) protection.update_bus(42, true);
    assert(protection.active());
    protection.update_bus(42, true);
    assert(!protection.active());
    protection.update_bus(11, true);
    assert(protection.fault_mask() == (1U << 5U));
    AdcScale scaling;
    assert(scaling.configure({12, 8, 3, 3300}));
    assert(scaling.full_scale() == 4095U);
    assert(std::fabs(scaling.voltage(4095) - 3.3F) < 1.0e-5F);
    assert(scaling.configure({12, 4, 0, 3300}));
    assert(scaling.full_scale() == 16380U);
    assert(!scaling.configure({7, 1, 0, 3300}));
    assert(CurrentLoop::adc_trigger_count(5000, 20000, 1000) == 4800U);
    assert(CurrentLoop::adc_trigger_count(100, 20000, 1000000000U) == 1U);
}

static void test_joint_reference_snapshots_and_lut_publication()
{
    MotorParameters parameters;
    assert(motor_parameters(MotorVariant::ProfileA, parameters));
    MotorAngleLutData lut;
    lut.valid = true;
    for (float& offset : lut.angle_offset_rad) offset = 0.1F;
    MotorAngleLutView view {&lut, false};
    MotorSensorCalibData sensors;
    sensors.output_angle_calib = &view;
    MotorCalibData reference;
    reference.joint_zero_angle = {true, 1.0F};
    reference.joint_offset_angle = {true, 0.2F};
    FeedbackProcessor feedback;
    assert(feedback.configure(parameters, {}, sensors, reference));
    MotorCalibData candidate;
    assert(!feedback.joint_reference_candidate(true, candidate));
    // Calibration still needs current/Vbus conversion before rotor fitting.
    assert(feedback.electrical_sample(1.65F, 1.65F, 1.65F, 1.0F));
    assert(!feedback.rotor_sample(0, 1));
    assert(feedback.joint_sample(std::sin(2.0F), std::cos(2.0F)));
    const float corrected = feedback.joint().single_turn_angle_calib;
    assert(std::fabs(corrected - 2.1F) < 1.0e-5F);
    assert(std::fabs(feedback.joint().multi_turn_angle - 0.9F) < 1.0e-5F);
    assert(feedback.joint_reference_candidate(true, candidate));
    assert(std::fabs(candidate.joint_zero_angle.offset - corrected) < 1.0e-5F);
    assert(candidate.joint_offset_angle.offset == 0.2F);
    assert(feedback.joint_reference_candidate(false, candidate));
    assert(std::fabs(candidate.joint_offset_angle.offset - 1.1F) < 1.0e-5F);
    assert(candidate.joint_zero_angle.offset == 1.0F);
    view.updating = true;
    assert(!feedback.joint_sample(std::sin(3.0F), std::cos(3.0F)));
    assert(feedback.joint().single_turn_angle_calib == corrected);
    view.updating = false;

    // Boot just below zero must become a small negative multi-turn angle.
    reference.joint_zero_angle.offset = 0.2F;
    reference.joint_offset_angle = {};
    sensors.output_angle_calib = nullptr;
    assert(feedback.configure(parameters, {}, sensors, reference));
    assert(feedback.joint_sample(std::sin(0.1F), std::cos(0.1F)));
    assert(feedback.joint().turn_count == -1);
    assert(std::fabs(feedback.joint().multi_turn_angle + 0.1F) < 1.0e-5F);
}

int main()
{
    test_all_presets();
    test_torque_mit_transition_and_disable();
    test_speed_uses_rotor_feedback();
    test_s_curve_limits();
    test_position_enable_starts_at_feedback();
    test_protection_and_adc_scaling();
    test_joint_reference_snapshots_and_lut_publication();
}
