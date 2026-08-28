#include <gimbal/attitude_ekf.h>
#include <gimbal/controller.h>
#include <gimbal/hall_calibration.h>
#include <gimbal/hall_sensor.h>
#include <gimbal/kinematics.h>
#include <gimbal/math.h>
#include <gimbal/motion_planner.h>
#include <gimbal/parameters.h>
#include <gimbal/safety_manager.h>
#include <gimbal/shared_topics.h>
#include <gimbal/thermal_controller.h>
#include <gimbal/voltage_foc.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <thread>

namespace {

[[nodiscard]] bool near(float left, float right, float tolerance)
{
    return std::abs(left - right) <= tolerance;
}

[[nodiscard]] bool test_hall_angle()
{
    gimbal::HallCalibration calibration {};
    calibration.offset = {2048.0F, 2048.0F};
    calibration.correction[0][0] = 0.001F;
    calibration.correction[1][1] = 0.001F;
    gimbal::HallAngleSensor sensor;
    if (!sensor.configure(calibration)) {
        return false;
    }
    gimbal::HallState state {};
    gimbal::HallSignalCalibrator calibrator;
    if (calibrator.add_sample({-1.0F, 2048.0F, 0U})) {
        return false;
    }
    const gimbal::HallRawSample first {3048.0F, 2048.0F, 1000U};
    const gimbal::HallRawSample second {2048.0F, 3048.0F, 2000U};
    return sensor.update(first, 0.001F, state) == gimbal::HallStatus::Ok
        && sensor.update(second, 0.1F, state) == gimbal::HallStatus::Ok
        && near(state.mechanical_angle_rad,
                0.5F * gimbal::math::kPi, 0.01F);
}

[[nodiscard]] bool test_hall_zero_calibration()
{
    gimbal::HallZeroCalibrator calibrator;
    int8_t direction = 0;
    if (!gimbal::HallZeroCalibrator::infer_mechanical_direction(
            0.0F, 0.2F, 0.0F, -0.2F, direction)
        || direction != -1) {
        return false;
    }
    constexpr float kMechanicalZero = 0.3F;
    constexpr float kElectricalZero = -0.4F;
    constexpr uint8_t kPolePairs = 7U;
    for (uint32_t index = 0U; index < 32U; ++index) {
        const float hall = -1.0F + 0.05F * index;
        const float mechanical = -hall + kMechanicalZero;
        const float electrical = gimbal::math::wrap_pi(
            kPolePairs * mechanical + kElectricalZero);
        if (!calibrator.add_mechanical_observation(
                hall, mechanical, direction)
            || !calibrator.add_electrical_observation(
                mechanical, electrical, kPolePairs)) {
            return false;
        }
    }
    gimbal::HallCalibration calibration {};
    gimbal::HallZeroCalibrationQuality mechanical_quality {};
    gimbal::HallZeroCalibrationQuality electrical_quality {};
    return calibrator.finish_mechanical(
               calibration, 0.999F, mechanical_quality)
        && calibrator.finish_electrical(
            calibration, 0.999F, electrical_quality)
        && calibration.mechanical_direction == -1
        && calibration.pole_pairs == kPolePairs
        && near(calibration.mechanical_zero_rad,
                kMechanicalZero, 1.0e-5F)
        && near(calibration.electrical_zero_rad,
                kElectricalZero, 1.0e-5F)
        && mechanical_quality.sample_count == 32U
        && electrical_quality.concentration > 0.999F;
}

[[nodiscard]] bool test_voltage_foc()
{
    gimbal::VoltageFoc foc;
    if (!foc.configure({12.0F, 0.85F})) {
        return false;
    }
    const gimbal::PhaseDuty neutral = foc.calculate(0.0F, 0.0F, 0.0F, 0.0F);
    const gimbal::PhaseDuty drive = foc.calculate(0.3F, 0.0F, 30.0F, 12.0F);
    const gimbal::PhaseDuty overflow = foc.calculate(
        0.3F, 3.4e38F, 3.4e38F, 12.0F);
    const float minimum_duty = std::fmin(
        drive.phase_u, std::fmin(drive.phase_v, drive.phase_w));
    const float maximum_duty = std::fmax(
        drive.phase_u, std::fmax(drive.phase_v, drive.phase_w));
    return near(neutral.phase_u, 0.5F, 1.0e-6F)
        && near(neutral.phase_v, 0.5F, 1.0e-6F)
        && near(neutral.phase_w, 0.5F, 1.0e-6F)
        && drive.phase_u >= 0.0F && drive.phase_u <= 1.0F
        && drive.phase_v >= 0.0F && drive.phase_v <= 1.0F
        && drive.phase_w >= 0.0F && drive.phase_w <= 1.0F
        && near(overflow.phase_u, 0.5F, 1.0e-6F)
        && near(overflow.phase_v, 0.5F, 1.0e-6F)
        && near(overflow.phase_w, 0.5F, 1.0e-6F)
        && maximum_duty - minimum_duty <= 0.851F;
}

[[nodiscard]] bool test_attitude_ekf()
{
    gimbal::AttitudeEkf filter;
    if (!filter.configure({}, {})) {
        return false;
    }
    gimbal::ImuSample sample {};
    sample.header.health = gimbal::Health::Valid;
    sample.acceleration_mps2.z = gimbal::math::kGravityMps2;
    sample.temperature_c = 45.0F;
    for (uint32_t index = 0U; index < 100U; ++index) {
        sample.header.capture_time_us = index * 1000U;
        const bool predicted = filter.predict(sample, 0.001F);
        const bool acceleration_updated = predicted
            && filter.update_accelerometer(sample);
        const bool bias_updated = acceleration_updated
            && filter.update_stationary_bias(sample);
        if (!bias_updated) {
            std::printf(
                "ekf iteration=%lu predict=%u accel=%u bias=%u\n",
                static_cast<unsigned long>(index), predicted ? 1U : 0U,
                acceleration_updated ? 1U : 0U,
                bias_updated ? 1U : 0U);
            return false;
        }
    }
    const gimbal::AttitudeState state = filter.state(100000U);
    const gimbal::AttitudeState stale_state = filter.state(200000U);
    sample.angular_rate_rad_s.x = 1.0F;
    const bool passed = !filter.update_stationary_bias(sample)
        && state.header.health == gimbal::Health::Valid
        && stale_state.header.health == gimbal::Health::Invalid
        && near(state.camera_in_world.w, 1.0F, 1.0e-4F)
        && state.attitude_variance.z > state.attitude_variance.x;
    if (!passed) {
        std::printf(
            "ekf: health=%u q.w=%f variance=(%f,%f,%f)\n",
            static_cast<unsigned>(state.header.health),
            state.camera_in_world.w, state.attitude_variance.x,
            state.attitude_variance.y, state.attitude_variance.z);
        return false;
    }

    gimbal::AttitudeEkf tilted_filter;
    if (!tilted_filter.configure({}, {})) return false;
    constexpr float kTilt = 0.1F;
    sample.angular_rate_rad_s = {};
    sample.acceleration_mps2 = {
        0.0F,
        std::sin(kTilt) * gimbal::math::kGravityMps2,
        std::cos(kTilt) * gimbal::math::kGravityMps2,
    };
    for (uint32_t index = 0U; index < 200U; ++index) {
        if (!tilted_filter.predict(sample, 0.001F)
            || !tilted_filter.update_accelerometer(sample)) {
            return false;
        }
    }
    const gimbal::Vector3 tilt = gimbal::math::rotation_vector(
        tilted_filter.state(200000U).camera_in_world);
    gimbal::ImuCalibration rotated_calibration {};
    rotated_calibration.gyro_sensor_to_camera.value[0][0] = 0.0F;
    rotated_calibration.gyro_sensor_to_camera.value[0][1] = -1.0F;
    rotated_calibration.gyro_sensor_to_camera.value[1][0] = 1.0F;
    rotated_calibration.gyro_sensor_to_camera.value[1][1] = 0.0F;
    gimbal::AttitudeEkf rotated_filter;
    if (!rotated_filter.configure({}, rotated_calibration)) return false;
    sample.angular_rate_rad_s = {1.0F, 0.0F, 0.0F};
    sample.acceleration_mps2 = {0.0F, 0.0F,
                                gimbal::math::kGravityMps2};
    sample.header.capture_time_us = 1000U;
    if (!rotated_filter.predict(sample, 0.001F)) return false;
    const gimbal::Vector3 rotated = gimbal::math::rotation_vector(
        rotated_filter.state(1000U).camera_in_world);
    return near(tilt.x, kTilt, 0.01F)
        && near(tilt.y, 0.0F, 0.01F)
        && near(tilt.z, 0.0F, 0.01F)
        && near(rotated.x, 0.0F, 1.0e-4F)
        && near(rotated.y, 0.001F, 1.0e-4F);
}

[[nodiscard]] bool test_kinematics()
{
    gimbal::KinematicConfig config {};
    config.joint_axis_handle[1] = gimbal::math::normalized(
        gimbal::Vector3 {0.0F, 1.0F, 0.2F});
    gimbal::NonOrthogonalKinematics model;
    if (!model.configure(config)) {
        return false;
    }
    gimbal::JointState joint {};
    joint.header.health = gimbal::Health::Valid;
    joint.valid_axis_mask = gimbal::kAllAxesMask;
    joint.angle_rad = {0.2F, -0.3F, 0.1F};
    const gimbal::Matrix3 camera_rotation =
        model.camera_in_handle(joint.angle_rad);
    gimbal::AttitudeState camera {};
    camera.header.health = gimbal::Health::Valid;
    camera.camera_in_world = gimbal::math::quaternion(camera_rotation);
    const gimbal::HandleState handle =
        model.estimate_handle(camera, joint, 1000U);
    gimbal::Matrix3 effort_mapping {};
    gimbal::Matrix3 motion_mapping {};
    const bool mapping_valid = model.calculate_control_mappings(
        joint.angle_rad, effort_mapping, motion_mapping);
    return handle.header.health == gimbal::Health::Valid
        && near(std::abs(handle.handle_in_world.w), 1.0F, 1.0e-3F)
        && mapping_valid
        && std::abs(gimbal::math::determinant(effort_mapping)) > 0.05F
        && std::abs(gimbal::math::determinant(motion_mapping)) > 0.05F
        && !model.is_singular(joint.angle_rad);
}

[[nodiscard]] bool test_dynamics_mapping()
{
    gimbal::DynamicsFeedforward dynamics;
    if (!dynamics.configure({})) {
        return false;
    }
    gimbal::JointState joint {};
    joint.header.health = gimbal::Health::Valid;
    joint.valid_axis_mask = gimbal::kAllAxesMask;
    gimbal::MotionReference reference {};
    reference.header.health = gimbal::Health::Valid;
    reference.angular_acceleration_rad_s2.x = 1.0F;
    gimbal::Matrix3 mapping {};
    mapping.value[0][0] = 2.0F;
    const gimbal::DynamicsOutput output = dynamics.calculate(
        joint, reference, mapping);
    gimbal::Matrix3 singular {};
    for (auto &row : singular.value) {
        for (float &element : row) element = 0.0F;
    }
    const gimbal::DynamicsOutput rejected = dynamics.calculate(
        joint, reference, singular);
    return output.health == gimbal::Health::Valid
        && near(output.torque_nm[0], 2.0F, 1.0e-5F)
        && rejected.health == gimbal::Health::Invalid;
}

[[nodiscard]] bool test_planner_and_controller()
{
    gimbal::MotionPlanner planner;
    if (!planner.configure({})) {
        return false;
    }
    planner.reset();
    if (planner.set_target({0.0F, 0.0F, 0.0F, 0.0F})) {
        return false;
    }
    if (!planner.set_target(gimbal::math::from_rotation_vector(
            {0.2F, 0.0F, 0.0F}))) {
        return false;
    }
    const gimbal::MotionReference reference = planner.update(0.005F, 5000U);
    if (std::abs(reference.angular_acceleration_rad_s2.x) > 0.401F) {
        return false;
    }
    gimbal::TwoDegreeController controller;
    if (!controller.configure({}, gimbal::MotorControlMode::VoltageFoc)) {
        return false;
    }
    if (controller.configure(
            {}, static_cast<gimbal::MotorControlMode>(0xFFU))) {
        return false;
    }
    if (!controller.configure({}, gimbal::MotorControlMode::VoltageFoc)) {
        return false;
    }
    gimbal::ControllerConfig invalid_controller {};
    invalid_controller.axis[0].notch_frequency_hz = 500.0F;
    if (gimbal::valid_controller_config(invalid_controller)) {
        return false;
    }
    gimbal::AttitudeState attitude {};
    attitude.header.health = gimbal::Health::Valid;
    gimbal::DynamicsOutput feedforward {};
    feedforward.health = gimbal::Health::Valid;
    const gimbal::Matrix3 identity {};
    const gimbal::MotorCommand command = controller.update({
        reference, attitude, feedforward, identity, 0.001F, 5000U,
    });
    const gimbal::MotorCommand wrong_period = controller.update({
        reference, attitude, feedforward, identity, 0.01F, 5000U,
    });
    gimbal::Matrix3 singular_mapping {};
    for (auto &row : singular_mapping.value) {
        for (float &element : row) element = 0.0F;
    }
    const gimbal::MotorCommand rejected_mapping = controller.update({
        reference, attitude, feedforward, singular_mapping,
        0.001F, 5000U,
    });
    const gimbal::MotorCommand idle = controller.idle(6000U);
    const gimbal::MotorCommand restarted = controller.update({
        reference, attitude, feedforward, identity, 0.001F, 7000U,
    });
    return command.header.health == gimbal::Health::Valid
        && command.enable && command.q_axis_command[0] > 0.0F
        && wrong_period.header.health == gimbal::Health::Fault
        && rejected_mapping.header.health == gimbal::Health::Fault
        && idle.header.health == gimbal::Health::Valid && !idle.enable
        && idle.header.sequence > rejected_mapping.header.sequence
        && restarted.header.sequence > idle.header.sequence
        && near(restarted.q_axis_command[0],
                command.q_axis_command[0], 1.0e-5F);
}

[[nodiscard]] gimbal::SafetyInput valid_safety_input()
{
    gimbal::SafetyInput input {};
    input.now_us = 1000U;
    input.factory_parameters_valid = true;
    input.heater_ready = true;
    input.estimator_valid = true;
    input.joint.header.health = gimbal::Health::Valid;
    input.joint.header.publish_time_us = 1000U;
    input.joint.valid_axis_mask = gimbal::kAllAxesMask;
    input.imu.header.health = gimbal::Health::Valid;
    input.imu.header.publish_time_us = 1000U;
    input.command.header.health = gimbal::Health::Valid;
    input.command.header.publish_time_us = 1000U;
    return input;
}

[[nodiscard]] bool test_safety_and_shared_memory()
{
    gimbal::SafetyManager safety;
    if (!safety.configure({}, {})) {
        return false;
    }
    gimbal::CapabilitySet invalid_capability {};
    invalid_capability.motor_mode =
        static_cast<gimbal::MotorControlMode>(0xFFU);
    gimbal::SafetyManager rejected_safety;
    if (rejected_safety.configure({}, invalid_capability)) {
        return false;
    }
    safety.start_self_test();
    gimbal::SafetyInput input = valid_safety_input();
    gimbal::SafetyOutput output = safety.update(input, 0.01F);
    if (output.state != gimbal::SystemState::Standby
        || output.active_faults != 0U) {
        return false;
    }
    gimbal::SafetyManager startup_safety;
    if (!startup_safety.configure({}, {})) {
        return false;
    }
    startup_safety.start_self_test();
    gimbal::SafetyInput missing_input {};
    missing_input.factory_parameters_valid = true;
    output = startup_safety.update(missing_input, 0.01F);
    if (output.state != gimbal::SystemState::SelfTest
        || output.latched_faults != 0U) {
        return false;
    }
    safety.request_arm();
    (void)safety.update(input, 0.01F);
    output = safety.update(input, 0.01F);
    gimbal::SafetyManager reset_safety;
    if (!reset_safety.configure({}, {})) {
        return false;
    }
    reset_safety.request_arm();
    if (!reset_safety.configure({}, {})) {
        return false;
    }
    reset_safety.start_self_test();
    const gimbal::SafetyOutput reset_output =
        reset_safety.update(input, 0.01F);
    gimbal::SnapshotTopic<gimbal::SafetyOutput> topic;
    gimbal::SafetyOutput copy {};
    if (output.state != gimbal::SystemState::Active
        || !output.allow_motor_output || !topic.publish(output)
        || !topic.read(copy)
        || copy.state != gimbal::SystemState::Active) {
        return false;
    }
    input.now_us = 2000U;
    input.joint.header.publish_time_us = 2000U;
    input.imu.header.publish_time_us = 2000U;
    input.command.header.publish_time_us = 2000U;
    input.joint.speed_rad_s[0] = 100.0F;
    input.deadline_missed = true;
    output = safety.update(input, 0.01F);
    return output.state == gimbal::SystemState::Fault
        && (output.first_fault.faults
            & gimbal::fault_mask(gimbal::Fault::Overspeed)) != 0U
        && (output.first_fault.faults
            & gimbal::fault_mask(gimbal::Fault::DeadlineMissed)) != 0U
        && output.first_fault.capture_time_us == 2000U
        && output.first_fault.joint_speed_rad_s[0] == 100.0F
        && !output.allow_motor_output
        && reset_output.state == gimbal::SystemState::Standby;
}

struct SharedTestValue {
    uint32_t sequence {0U};
    uint32_t inverse {UINT32_MAX};
};

[[nodiscard]] bool test_shared_memory_concurrency()
{
    constexpr uint32_t kIterations = 20000U;
    gimbal::SnapshotTopic<SharedTestValue> topic;
    std::atomic<bool> done {false};
    std::atomic<bool> valid {true};
    std::thread writer([&topic, &done]() {
        for (uint32_t sequence = 1U; sequence <= kIterations;
             ++sequence) {
            const SharedTestValue value {sequence, ~sequence};
            while (!topic.publish(value)) {
                std::this_thread::yield();
            }
        }
        done.store(true, std::memory_order_release);
    });
    std::thread reader([&topic, &done, &valid]() {
        SharedTestValue value {};
        while (!done.load(std::memory_order_acquire)) {
            if (topic.read(value) && value.inverse != ~value.sequence) {
                valid.store(false, std::memory_order_release);
                break;
            }
        }
    });
    writer.join();
    reader.join();
    SharedTestValue final {};
    return valid.load(std::memory_order_acquire)
        && topic.read(final) && final.sequence == kIterations
        && final.inverse == ~final.sequence;
}

[[nodiscard]] bool test_thermal_and_parameters()
{
    gimbal::ThermalConfig config {};
    config.stable_time_s = 0.2F;
    gimbal::ImuThermalController thermal;
    if (!thermal.configure(config)) {
        return false;
    }
    thermal.start(50.0F);
    gimbal::ThermalOutput output {};
    for (size_t index = 0U; index < 4U; ++index) {
        output = thermal.update(50.0F, 0.1F);
    }
    const gimbal::UserParameters user {};
    auto record = gimbal::make_parameter_record(
        gimbal::kUserParameterMagic, gimbal::kUserParameterSchema,
        7U, user);
    const bool initially_valid = gimbal::valid_parameter_record(
        record, gimbal::kUserParameterMagic,
        gimbal::kUserParameterSchema);
    record.payload.follow_gain.x += 0.1F;
    gimbal::FactoryParameters invalid_factory {};
    invalid_factory.capabilities.motor_mode =
        static_cast<gimbal::MotorControlMode>(0xFFU);
    gimbal::FactoryParameters mismatched_poles {};
    mismatched_poles.dynamics.pole_pairs[0] = 8U;
    gimbal::ThermalConfig invalid_thermal {};
    invalid_thermal.maximum_temperature_c = 200.0F;
    gimbal::ImuCalibration invalid_imu {};
    invalid_imu.gyro_sensor_to_camera.value[0][0] = 0.0F;
    gimbal::HallCalibration invalid_hall {};
    invalid_hall.maximum_speed_rad_s =
        std::numeric_limits<float>::infinity();
    gimbal::SafetyConfig invalid_safety {};
    invalid_safety.maximum_phase_current_a =
        std::numeric_limits<float>::infinity();
    gimbal::ThermalConfig infinite_thermal {};
    infinite_thermal.proportional_gain =
        std::numeric_limits<float>::infinity();
    gimbal::FactoryParameters factory {};
    gimbal::UserParameters excessive_user {};
    excessive_user.follow_gain.x = factory.maximum_follow_gain.x + 0.1F;
    return output.is_ready && output.heater_duty == 0.0F
        && initially_valid
        && gimbal::valid_factory_parameters(factory)
        && !gimbal::valid_factory_parameters(invalid_factory)
        && !gimbal::valid_factory_parameters(mismatched_poles)
        && !gimbal::valid_thermal_config(invalid_thermal)
        && !gimbal::valid_thermal_config(infinite_thermal)
        && !gimbal::valid_hall_calibration(invalid_hall)
        && !gimbal::valid_safety_config(invalid_safety)
        && !gimbal::valid_imu_calibration(invalid_imu)
        && !gimbal::valid_user_parameters(excessive_user, factory)
        && !gimbal::valid_parameter_record(
            record, gimbal::kUserParameterMagic,
            gimbal::kUserParameterSchema);
}

} // namespace

int main()
{
    struct TestCase {
        const char *name;
        bool (*run)();
    };
    const TestCase tests[] {
        {"hall_angle", test_hall_angle},
        {"hall_zero_calibration", test_hall_zero_calibration},
        {"voltage_foc", test_voltage_foc},
        {"attitude_ekf", test_attitude_ekf},
        {"kinematics", test_kinematics},
        {"dynamics_mapping", test_dynamics_mapping},
        {"planner_controller", test_planner_and_controller},
        {"safety_shared_memory", test_safety_and_shared_memory},
        {"shared_memory_concurrency", test_shared_memory_concurrency},
        {"thermal_parameters", test_thermal_and_parameters},
    };
    bool passed = true;
    for (const TestCase &test : tests) {
        if (!test.run()) {
            std::printf("gimbal core subtest failed: %s\n", test.name);
            passed = false;
        }
    }
    std::printf("gimbal core test: %s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
