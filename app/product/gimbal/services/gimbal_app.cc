#include "services/gimbal_app.h"

#include "board/board_devices.h"
#include "services/parameter_store.h"

#include <gimbal/attitude_ekf.h>
#include <gimbal/controller.h>
#include <gimbal/dynamics.h>
#include <gimbal/hall_sensor.h>
#include <gimbal/kinematics.h>
#include <gimbal/math.h>
#include <gimbal/motion_planner.h>
#include <gimbal/shared_topics.h>
#include <gimbal/thermal_controller.h>
#include <imu/icm40609d.h>
#include <init.h>
#include <log.h>
#include <osal.h>
#include <sensor_core.h>
#include <system/watchdog.h>
#include <arch/arm/cortex_m/fault.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cmath>

namespace app {
namespace {

constexpr float kSensorPeriodS = 1.0F / CONFIG_GIMBAL_SENSOR_HZ;
constexpr float kControlPeriodS = 1.0F / CONFIG_GIMBAL_CONTROL_HZ;
constexpr float kPlannerPeriodS = 1.0F / CONFIG_GIMBAL_PLANNER_HZ;
constexpr float kThermalPeriodS = 1.0F / CONFIG_GIMBAL_THERMAL_HZ;
constexpr float kSupervisorPeriodS = 1.0F / CONFIG_GIMBAL_SUPERVISOR_HZ;
constexpr uint32_t kMicrosecondsPerSecond = 1000000U;
constexpr float kHallFilterNyquistMargin = 0.45F;
constexpr float kHallAnglePerSampleMargin = 0.8F;
constexpr osal::Priority kClosedLoopPriority = 7U;
constexpr osal::Priority kSupervisorPriority = 4U;
constexpr osal::Priority kPlannerPriority = 3U;
constexpr osal::Priority kThermalPriority = 2U;
constexpr uint32_t kSensorStartupTimeoutMs = 20U;
static_assert(kClosedLoopPriority > kSupervisorPriority);
static_assert(kSupervisorPriority > kPlannerPriority);
static_assert(kPlannerPriority > kThermalPriority);
static_assert(kClosedLoopPriority <= osal::kPriorityMax);
static_assert(CONFIG_GIMBAL_SENSOR_HZ % CONFIG_GIMBAL_CONTROL_HZ == 0,
              "Sensor rate must be an integer multiple of control rate");
constexpr size_t kSamplesPerControl =
    CONFIG_GIMBAL_SENSOR_HZ / CONFIG_GIMBAL_CONTROL_HZ;
static_assert(kSamplesPerControl > 0U);
constexpr float kAccelScale = gimbal::math::kGravityMps2 / 2048.0F;
constexpr float kGyroScale = gimbal::math::kPi / (180.0F * 16.4F);
constexpr float kTemperatureScale = 1.0F / 132.48F;
constexpr float kTemperatureOffsetC = 25.0F;

#if defined(CONFIG_GIMBAL_PHASE_CURRENT_SENSE)
constexpr bool kBuildHasPhaseCurrent = true;
#else
constexpr bool kBuildHasPhaseCurrent = false;
#endif
#if defined(CONFIG_GIMBAL_VBUS_SENSE)
constexpr bool kBuildHasBusVoltage = true;
#else
constexpr bool kBuildHasBusVoltage = false;
#endif
#if defined(CONFIG_GIMBAL_DRIVER_TEMP_SENSE)
constexpr bool kBuildHasDriverTemperature = true;
#else
constexpr bool kBuildHasDriverTemperature = false;
#endif
#if defined(CONFIG_GIMBAL_MOS_TEMP_SENSE)
constexpr bool kBuildHasMosTemperature = true;
#else
constexpr bool kBuildHasMosTemperature = false;
#endif

constexpr bool kBuildNeedsExternalFeedback =
    kBuildHasPhaseCurrent || kBuildHasBusVoltage
    || kBuildHasDriverTemperature || kBuildHasMosTemperature;

#if defined(CONFIG_APP_WATCHDOG)
enum class WatchdogClient : size_t {
    Main = 0U,
    ClosedLoop,
    Planner,
    Thermal,
    Supervisor,
    Count,
};

constexpr const char *kWatchdogNames[] {
    "g_main", "g_closed", "g_planner", "g_thermal", "g_supervisor",
};
std::array<system_watchdog::ClientId,
           static_cast<size_t>(WatchdogClient::Count)> watchdog_clients {};

int register_watchdog_clients()
{
    watchdog_clients.fill(system_watchdog::kInvalidClient);
    const uint32_t timeout_ms = CONFIG_APP_WATCHDOG_TIMEOUT_MS / 2U;
    for (size_t index = 0U; index < watchdog_clients.size(); ++index) {
        watchdog_clients[index] = system_watchdog::register_client(
            kWatchdogNames[index], timeout_ms);
        if (watchdog_clients[index] == system_watchdog::kInvalidClient) {
            for (size_t registered = 0U; registered < index; ++registered) {
                (void)system_watchdog::unregister_client(
                    watchdog_clients[registered]);
            }
            return -1;
        }
    }
    return 0;
}

SYS_INIT(register_watchdog_clients, INITCALL_LEVEL_APPLICATION, 90);

void heartbeat_or_panic(WatchdogClient client)
{
    const size_t index = static_cast<size_t>(client);
    if (!system_watchdog::heartbeat(watchdog_clients[index])) {
        hal::fault::panic(hal::fault::FatalReason::WatchdogExpired,
                          static_cast<int32_t>(index),
                          kWatchdogNames[index], 0U);
    }
}
#endif

[[nodiscard]] bool capabilities_match_build(
    const gimbal::CapabilitySet &capabilities)
{
    return capabilities.motor_mode == gimbal::MotorControlMode::VoltageFoc
        && capabilities.has_phase_current == kBuildHasPhaseCurrent
        && capabilities.has_bus_voltage == kBuildHasBusVoltage
        && capabilities.has_driver_temperature
            == kBuildHasDriverTemperature
        && capabilities.has_mos_temperature == kBuildHasMosTemperature;
}

[[nodiscard]] bool hardware_revision_matches_board(
    const gimbal::FactoryParameters &parameters)
{
    return parameters.hardware_revision == board::kHardwareRevision;
}

[[nodiscard]] bool runtime_rates_match_factory(
    const gimbal::FactoryParameters &parameters)
{
    const float hall_hz =
        static_cast<float>(CONFIG_GIMBAL_CONTROL_HZ);
    const uint32_t sensor_period_us =
        kMicrosecondsPerSecond / CONFIG_GIMBAL_CONTROL_HZ;
    const uint32_t planner_period_us =
        kMicrosecondsPerSecond / CONFIG_GIMBAL_PLANNER_HZ;
    if (std::abs(parameters.controller.sample_frequency_hz
                 - static_cast<float>(CONFIG_GIMBAL_CONTROL_HZ)) > 0.5F
        || parameters.ekf.maximum_sample_period_s < kSensorPeriodS
        || parameters.safety.sensor_timeout_us < 2U * sensor_period_us
        || parameters.safety.command_timeout_us < 2U * planner_period_us) {
        return false;
    }
    for (const gimbal::HallCalibration &hall : parameters.hall) {
        if (hall.speed_filter_hz
                >= kHallFilterNyquistMargin * hall_hz
            || hall.maximum_speed_rad_s * kControlPeriodS
                >= kHallAnglePerSampleMargin * gimbal::math::kPi) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] uint32_t now_us()
{
    return osal::Kernel::uptime_ms() * 1000U;
}

[[nodiscard]] gimbal::FactoryParameters commissioning_parameters()
{
    gimbal::FactoryParameters parameters {};
    for (gimbal::HallCalibration &hall : parameters.hall) {
        hall.offset = {2048.0F, 2048.0F};
        hall.correction[0][0] = 0.001F;
        hall.correction[1][1] = 0.001F;
        hall.minimum_signal_norm = 0.2F;
        hall.maximum_signal_norm = 2.0F;
    }
    parameters.kinematics.joint_axis_handle[1] =
        gimbal::math::normalized(
            gimbal::Vector3 {0.0F, 0.9848078F, 0.1736482F});
    parameters.kinematics.joint_axis_handle[2] =
        gimbal::math::normalized(
            gimbal::Vector3 {0.0871557F, 0.0F, 0.9961947F});
    parameters.capabilities.motor_mode =
        gimbal::MotorControlMode::VoltageFoc;
    parameters.capabilities.has_imu_heater = true;
    parameters.hardware_revision = board::kHardwareRevision;
    return parameters;
}

[[nodiscard]] gimbal::VoltageMotorConfig motor_config(
    const gimbal::FactoryParameters &parameters, size_t axis)
{
    gimbal::VoltageMotorConfig config {};
    const board::MotorPwmTopology &topology =
        board::kMotorTopologies[axis];
    config.foc = parameters.voltage_foc[axis];
    config.pwm_period_counts = topology.period_counts;
    config.phase_u = topology.phase_u;
    config.phase_v = topology.phase_v;
    config.phase_w = topology.phase_w;
    return config;
}

[[nodiscard]] gimbal::UserParameters default_user_parameters(
    const gimbal::FactoryParameters &factory)
{
    gimbal::UserParameters parameters {};
    parameters.motion_limits = factory.factory_motion_limits;
    return parameters;
}

struct TaskDefinition {
    const char *name;
    osal::PeriodicEntry entry;
    void *context;
    void *stack;
    size_t stack_size;
    uint32_t frequency_hz;
    osal::Priority priority;
};

struct ImuSensorFrame {
    SensorBatchCore::FrameHeader header {};
    imu::Icm40609dRawFrame imu {};
};

enum class OutputPermission : uint8_t {
    Denied = 0U,
    Allowed,
};

[[nodiscard]] bool start_task(osal::PeriodicThread &thread,
                              const TaskDefinition &definition)
{
    osal::PeriodicThreadConfig config {};
    config.name = definition.name;
    config.entry = definition.entry;
    config.context = definition.context;
    config.stack_buffer = definition.stack;
    config.stack_size_bytes = definition.stack_size;
    config.frequency_hz = definition.frequency_hz;
    config.priority = definition.priority;
    return thread.start(config) && thread.startup() == 0;
}

void log_task_diagnostics(const char *name,
                          const osal::PeriodicThread &thread)
{
    const osal::StackStats stack = thread.stack_stats();
    LOGI("gimbal", "%s stack_free=%lu/%lu missed=%lu",
         name,
         static_cast<unsigned long>(stack.minimum_free_bytes),
         static_cast<unsigned long>(stack.total_bytes),
         static_cast<unsigned long>(thread.missed()));
}

class GimbalRuntime {
public:
    [[nodiscard]] int start();
    void stop();
    [[nodiscard]] bool request_arm();
    void request_disarm();
    [[nodiscard]] gimbal::SafetyOutput safety_status() const;
    void print_diagnostics() const;

private:
    [[nodiscard]] bool configure();
    [[nodiscard]] bool configure_motors();
    [[nodiscard]] bool start_heater();
    [[nodiscard]] bool start_tasks();
    [[nodiscard]] bool start_background_tasks();
    [[nodiscard]] bool start_sensor_pipeline();
    [[nodiscard]] bool start_supervisor();
    void wait_for_first_sensor_batch();
    void stop_tasks();
    void closed_loop_batch(const SensorBatchCore::BatchView& batch);
    void publish_joint_state(uint32_t timestamp);
    [[nodiscard]] bool process_imu_frame(
        const ImuSensorFrame& frame, const gimbal::JointState& joint,
        float maximum_joint_speed, gimbal::ImuSample& sample);
    void publish_estimator_state(
        uint32_t timestamp, const gimbal::JointState& joint);
    void planner_tick();
    void control_tick();
    void thermal_tick();
    void supervisor_tick();
    [[nodiscard]] bool write_heater_duty(float duty);
    void read_halls(gimbal::JointState &joint, uint32_t timestamp,
                    float sample_period_s);
    [[nodiscard]] bool apply_motor_command(
        const gimbal::MotorCommand &command,
        const gimbal::JointState &joint,
        OutputPermission permission);
    void disarm_motors();
    [[nodiscard]] gimbal::Quaternion planner_target() const;
    [[nodiscard]] bool initialize_planner_reference();
    [[nodiscard]] bool joint_limit_exceeded(
        const gimbal::JointState &joint) const;
    [[nodiscard]] bool deadline_missed_since_last_check();

    static void closed_loop_entry(
        void* context, const SensorBatchCore::BatchView& batch);
    static bool imu_source_prepare(
        void* context, SensorBatchCore::CompletionFn completion,
        void* completion_argument);
    static bool imu_source_start(void* context, void* destination);
    static void imu_source_stop(void* context);
    static void imu_dma_complete(void* context, hal::Status status);
    static void planner_entry(void *context,
                              const osal::PeriodicStats &);
    static void thermal_entry(void *context,
                              const osal::PeriodicStats &);
    static void supervisor_entry(void *context,
                                 const osal::PeriodicStats &);

    ParameterStore parameter_store_ {};
    gimbal::FactoryParameters factory_ {};
    gimbal::UserParameters user_ {};
    gimbal::SharedTopics topics_ {};
    std::array<gimbal::HallAngleSensor, gimbal::kAxisCount> hall_ {};
    gimbal::AttitudeEkf estimator_ {};
    gimbal::NonOrthogonalKinematics kinematics_ {};
    gimbal::DynamicsFeedforward dynamics_ {};
    gimbal::MotionPlanner planner_ {};
    gimbal::TwoDegreeController controller_ {};
    gimbal::ImuThermalController thermal_ {};
    gimbal::SafetyManager safety_ {};
    gimbal::Quaternion lock_target_ {};
    gimbal::Quaternion camera_origin_ {};
    gimbal::Quaternion handle_origin_ {};
    gimbal::ProductMode active_mode_ {gimbal::ProductMode::Lock};
    SensorBatchCore sensor_core_ {};
    osal::PeriodicThread planner_task_ {};
    osal::PeriodicThread thermal_task_ {};
    osal::PeriodicThread supervisor_task_ {};
    alignas(std::max_align_t)
        uint8_t closed_loop_stack_[CONFIG_GIMBAL_CLOSED_LOOP_STACK_SIZE] {};
    alignas(std::max_align_t)
        uint8_t planner_stack_[CONFIG_GIMBAL_PLANNER_STACK_SIZE] {};
    alignas(std::max_align_t)
        uint8_t thermal_stack_[CONFIG_GIMBAL_THERMAL_STACK_SIZE] {};
    alignas(std::max_align_t)
        uint8_t supervisor_stack_[CONFIG_GIMBAL_SUPERVISOR_STACK_SIZE] {};
    uint32_t joint_sequence_ {0U};
    uint32_t thermal_sequence_ {0U};
    uint32_t feedback_sequence_ {0U};
    uint32_t last_sensor_capture_time_us_ {0U};
    std::array<uint32_t, 7U> missed_baseline_ {};
    uint32_t last_closed_loop_fault_count_ {0U};
    SensorBatchCore::CompletionFn imu_completion_ {nullptr};
    void* imu_completion_argument_ {nullptr};
    std::array<ImuSensorFrame, kSamplesPerControl> sensor_buffer_a_ {};
    std::array<ImuSensorFrame, kSamplesPerControl> sensor_buffer_b_ {};
    std::atomic<uint32_t> closed_loop_batches_ {0U};
    std::atomic<bool> motor_enable_requested_ {false};
    bool factory_valid_ {false};
    bool motors_armed_ {false};
    bool planner_reference_initialized_ {false};
    bool has_sensor_capture_time_ {false};
    bool heater_output_fault_ {false};
    bool started_ {false};
};

GimbalRuntime runtime;

bool GimbalRuntime::configure_motors()
{
    auto &roll = board::roll_motor();
    auto &pitch = board::pitch_motor();
    auto &yaw = board::yaw_motor();
    const int roll_deinit = roll.deinit();
    const int pitch_deinit = pitch.deinit();
    const int yaw_deinit = yaw.deinit();
    if (roll_deinit != 0 || pitch_deinit != 0 || yaw_deinit != 0) {
        return false;
    }
    if (roll.init(motor_config(factory_, 0U)) != 0) {
        return false;
    }
    if (pitch.init(motor_config(factory_, 1U)) != 0) {
        (void)roll.deinit();
        return false;
    }
    if (yaw.init(motor_config(factory_, 2U)) != 0) {
        (void)pitch.deinit();
        (void)roll.deinit();
        return false;
    }
    return true;
}

bool GimbalRuntime::configure()
{
    motor_enable_requested_.store(false, std::memory_order_release);
    closed_loop_batches_.store(0U, std::memory_order_release);
    heater_output_fault_ = false;
    factory_valid_ = false;
    factory_ = commissioning_parameters();
    user_ = default_user_parameters(factory_);
    if (parameter_store_.initialize()) {
        factory_valid_ = parameter_store_.load(factory_, user_);
        if (factory_valid_
            && (!capabilities_match_build(factory_.capabilities)
                || !hardware_revision_matches_board(factory_))) {
            factory_ = commissioning_parameters();
            user_ = default_user_parameters(factory_);
            factory_valid_ = false;
        }
        if (!parameter_store_.user_valid()) {
            user_ = default_user_parameters(factory_);
        }
    }
    bool valid = configure_motors();
    last_sensor_capture_time_us_ = 0U;
    has_sensor_capture_time_ = false;
    missed_baseline_.fill(0U);
    last_closed_loop_fault_count_ = 0U;
    valid = runtime_rates_match_factory(factory_) && valid;
    for (size_t axis = 0U; axis < gimbal::kAxisCount; ++axis) {
        valid = hall_[axis].configure(factory_.hall[axis]) && valid;
    }
    valid = estimator_.configure(factory_.ekf, factory_.imu) && valid;
    valid = kinematics_.configure(factory_.kinematics) && valid;
    valid = dynamics_.configure(factory_.dynamics) && valid;
    valid = planner_.configure(user_.motion_limits) && valid;
    valid = controller_.configure(
        factory_.controller, factory_.capabilities.motor_mode) && valid;
    valid = thermal_.configure(factory_.thermal) && valid;
    valid = safety_.configure(
        factory_.safety, factory_.capabilities) && valid;
    planner_.reset();
    planner_reference_initialized_ = false;
    thermal_.start(kTemperatureOffsetC);
    safety_.start_self_test();
    (void)topics_.capabilities.publish(factory_.capabilities);
    (void)topics_.safety_output.publish(safety_.output());
    return valid;
}

bool GimbalRuntime::start_heater()
{
    auto &pwm = board::imu_heater_pwm();
    const hal::Status zero_status = pwm.set_pulse(0U);
    const hal::Status disable_status = pwm.disable_output();
    const hal::Status stop_status = pwm.stop();
    if (zero_status != hal::Status::Ok
        || disable_status != hal::Status::Ok
        || stop_status != hal::Status::Ok) {
        return false;
    }
    if (!factory_valid_ || !factory_.capabilities.has_imu_heater) {
        return true;
    }
    if (pwm.start() != hal::Status::Ok
        || pwm.enable_output() != hal::Status::Ok) {
        (void)pwm.disable_output();
        (void)pwm.stop();
        return false;
    }
    return true;
}

bool GimbalRuntime::imu_source_prepare(
    void* context, SensorBatchCore::CompletionFn completion,
    void* completion_argument)
{
    auto* runtime = static_cast<GimbalRuntime*>(context);
    runtime->imu_completion_ = completion;
    runtime->imu_completion_argument_ = completion_argument;
    if (board::imu().begin_async(imu_dma_complete, runtime)
        != hal::Status::Ok) {
        runtime->imu_completion_ = nullptr;
        runtime->imu_completion_argument_ = nullptr;
        return false;
    }
    return true;
}

bool GimbalRuntime::imu_source_start(void*, void* destination)
{
    if (destination == nullptr) {
        return false;
    }
    auto* frame = static_cast<imu::Icm40609dRawFrame*>(destination);
    return board::imu().start_read_async(*frame) == hal::Status::Ok;
}

void GimbalRuntime::imu_source_stop(void* context)
{
    auto* runtime = static_cast<GimbalRuntime*>(context);
    (void)board::imu().end_async();
    runtime->imu_completion_ = nullptr;
    runtime->imu_completion_argument_ = nullptr;
}

void GimbalRuntime::imu_dma_complete(void* context, hal::Status status)
{
    auto* runtime = static_cast<GimbalRuntime*>(context);
    if (runtime->imu_completion_ != nullptr) {
        runtime->imu_completion_(
            runtime->imu_completion_argument_,
            status == hal::Status::Ok
                ? SensorBatchCore::SourceResult::Success
                : SensorBatchCore::SourceResult::Failure);
    }
}

bool GimbalRuntime::start_background_tasks()
{
    const TaskDefinition tasks[] {
        {"g_planner", planner_entry, this, planner_stack_,
         sizeof(planner_stack_), CONFIG_GIMBAL_PLANNER_HZ,
         kPlannerPriority},
        {"g_thermal", thermal_entry, this, thermal_stack_,
         sizeof(thermal_stack_), CONFIG_GIMBAL_THERMAL_HZ,
         kThermalPriority},
    };
    osal::PeriodicThread *threads[] {
        &planner_task_, &thermal_task_,
    };
    for (size_t index = 0U; index < std::size(tasks); ++index) {
        if (!start_task(*threads[index], tasks[index])) {
            return false;
        }
    }

    return true;
}

bool GimbalRuntime::start_sensor_pipeline()
{
    auto &sensor_timer = board::sensor_timer();
    if (sensor_timer.stop() != hal::Status::Ok) {
        return false;
    }
    SensorBatchCore::Config sensor_config {};
    sensor_config.name = "g_closed";
    sensor_config.timer = &sensor_timer;
    sensor_config.entry = closed_loop_entry;
    sensor_config.entry_context = this;
    sensor_config.source_count = 1U;
    sensor_config.sources[0] = {
        imu_source_prepare, imu_source_start, imu_source_stop, this,
        offsetof(ImuSensorFrame, imu), sizeof(imu::Icm40609dRawFrame),
    };
    sensor_config.buffers = {
        sensor_buffer_a_.data(), sensor_buffer_b_.data(),
    };
    sensor_config.frame_size = sizeof(ImuSensorFrame);
    sensor_config.buffer_capacity = kSamplesPerControl;
    sensor_config.buffer_bytes = sizeof(sensor_buffer_a_);
    sensor_config.sample_frequency_hz = CONFIG_GIMBAL_SENSOR_HZ;
    sensor_config.consumer_frequency_hz = CONFIG_GIMBAL_CONTROL_HZ;
    sensor_config.stack_buffer = closed_loop_stack_;
    sensor_config.stack_size = sizeof(closed_loop_stack_);
    sensor_config.priority = kClosedLoopPriority;
    if (!sensor_core_.configure(sensor_config)
        || sensor_core_.start() != 0) {
        return false;
    }
    if (sensor_timer.start() != hal::Status::Ok) {
        (void)sensor_core_.stop();
        return false;
    }
    return true;
}

void GimbalRuntime::wait_for_first_sensor_batch()
{
    for (uint32_t elapsed_ms = 0U;
         elapsed_ms < kSensorStartupTimeoutMs; ++elapsed_ms) {
        if (closed_loop_batches_.load(std::memory_order_acquire) != 0U) {
            return;
        }
        osal::this_thread::sleep_for(1U);
    }
}

bool GimbalRuntime::start_supervisor()
{
    const TaskDefinition task {
        "g_supervisor", supervisor_entry, this, supervisor_stack_,
        sizeof(supervisor_stack_), CONFIG_GIMBAL_SUPERVISOR_HZ,
        kSupervisorPriority,
    };
    return start_task(supervisor_task_, task);
}

bool GimbalRuntime::start_tasks()
{
    if (!start_background_tasks() || !start_sensor_pipeline()) {
        return false;
    }
    wait_for_first_sensor_batch();
    return start_supervisor();
}

int GimbalRuntime::start()
{
    if (started_) {
        return 0;
    }
    if (!configure() || !start_heater() || !start_tasks()) {
        stop();
        return -1;
    }
    started_ = true;
    return 0;
}

void GimbalRuntime::stop_tasks()
{
    (void)board::sensor_timer().stop();
    if (sensor_core_.stop() != 0) {
        hal::fault::panic(
            hal::fault::FatalReason::ThreadShutdownTimeout,
            0, "gimbal_sensor_core", 0U);
    }
    supervisor_task_.destroy();
    thermal_task_.destroy();
    planner_task_.destroy();
}

void GimbalRuntime::stop()
{
    motor_enable_requested_.store(false, std::memory_order_release);
    safety_.request_disarm();
    stop_tasks();
    disarm_motors();
    (void)board::roll_motor().deinit();
    (void)board::pitch_motor().deinit();
    (void)board::yaw_motor().deinit();
    auto &heater = board::imu_heater_pwm();
    (void)heater.set_pulse(0U);
    (void)heater.disable_output();
    (void)heater.stop();
    thermal_.stop();
    started_ = false;
}

void GimbalRuntime::read_halls(gimbal::JointState &joint,
                               uint32_t timestamp,
                               float sample_period_s)
{
    gimbal::HallRawSample samples[gimbal::kAxisCount] {};
    const bool read_ok[gimbal::kAxisCount] {
        board::roll_hall().read(samples[0]),
        board::pitch_hall().read(samples[1]),
        board::yaw_hall().read(samples[2]),
    };
    for (size_t axis = 0U; axis < gimbal::kAxisCount; ++axis) {
        samples[axis].capture_time_us = timestamp;
        gimbal::HallState state {};
        if (read_ok[axis]
            && hall_[axis].update(samples[axis], sample_period_s, state)
                == gimbal::HallStatus::Ok) {
            joint.angle_rad[axis] = state.mechanical_angle_rad;
            joint.speed_rad_s[axis] = state.speed_rad_s;
            joint.electrical_angle_rad[axis] = state.electrical_angle_rad;
            joint.valid_axis_mask |= static_cast<uint8_t>(1U << axis);
        }
    }
}

void GimbalRuntime::publish_joint_state(uint32_t timestamp)
{
    float sample_period_s = kControlPeriodS;
    if (has_sensor_capture_time_) {
        const uint32_t elapsed_us =
            timestamp - last_sensor_capture_time_us_;
        if (elapsed_us != 0U) {
            sample_period_s = static_cast<float>(elapsed_us) * 1.0e-6F;
        }
    }
    last_sensor_capture_time_us_ = timestamp;
    has_sensor_capture_time_ = true;
    gimbal::JointState joint {};
    joint.header.sequence = ++joint_sequence_;
    joint.header.capture_time_us = timestamp;
    joint.header.publish_time_us = timestamp;
    read_halls(joint, timestamp, sample_period_s);
    joint.header.health = joint.valid_axis_mask == gimbal::kAllAxesMask
        ? gimbal::Health::Valid : gimbal::Health::Fault;
    joint.header.valid_flags = joint.valid_axis_mask;
    (void)topics_.joint_state.publish(joint);
}

bool GimbalRuntime::process_imu_frame(
    const ImuSensorFrame& frame, const gimbal::JointState& joint,
    float maximum_joint_speed, gimbal::ImuSample& sample)
{
    sample = {};
    sample.header.sequence = frame.header.sequence;
    sample.header.capture_time_us = frame.header.capture_time_us;
    sample.header.publish_time_us = now_us();
    if ((frame.header.valid_sources & 0x01U) == 0U) {
        sample.header.health = gimbal::Health::Fault;
        return false;
    }
    imu::ImuData raw {};
    if (!board::imu().decode(frame.imu, raw)) {
        sample.header.health = gimbal::Health::Fault;
        return false;
    }
    sample.acceleration_mps2 = {
        raw.accel[0] * kAccelScale,
        raw.accel[1] * kAccelScale,
        raw.accel[2] * kAccelScale,
    };
    sample.angular_rate_rad_s = {
        raw.gyro[0] * kGyroScale,
        raw.gyro[1] * kGyroScale,
        raw.gyro[2] * kGyroScale,
    };
    sample.temperature_c = raw.temp * kTemperatureScale
        + kTemperatureOffsetC;
    sample.header.health = gimbal::Health::Valid;
    sample.header.valid_flags = gimbal::kAllAxesMask;
    if (!estimator_.predict(sample, kSensorPeriodS)) {
        sample.header.health = gimbal::Health::Fault;
        return false;
    }
    (void)estimator_.update_accelerometer(sample);
    if (joint.header.health == gimbal::Health::Valid
        && maximum_joint_speed < 0.02F) {
        (void)estimator_.update_stationary_bias(sample);
    }
    return true;
}

void GimbalRuntime::publish_estimator_state(
    uint32_t timestamp, const gimbal::JointState& joint)
{
    const gimbal::AttitudeState attitude = estimator_.state(timestamp);
    (void)topics_.attitude_state.publish(attitude);
    if (joint.header.health == gimbal::Health::Valid) {
        const gimbal::HandleState handle = kinematics_.estimate_handle(
            attitude, joint, timestamp);
        (void)topics_.handle_state.publish(handle);
    }
}

void GimbalRuntime::closed_loop_batch(
    const SensorBatchCore::BatchView& batch)
{
    const SensorBatchCore::Diagnostics diagnostics =
        sensor_core_.diagnostics();
    const uint32_t fault_count = diagnostics.source_error_count
        + diagnostics.trigger_overrun_count
        + diagnostics.batch_overrun_count
        + diagnostics.dispatch_error_count
        + diagnostics.consumer_missed_count;
    const bool batch_shape_valid =
        batch.frames != nullptr
        && batch.frame_size == sizeof(ImuSensorFrame)
        && batch.frame_count == kSamplesPerControl;
    if (!batch_shape_valid || fault_count != last_closed_loop_fault_count_) {
        last_closed_loop_fault_count_ = fault_count;
        motor_enable_requested_.store(false, std::memory_order_release);
        controller_.reset();
        disarm_motors();
    }
    if (!batch_shape_valid) {
        return;
    }

    publish_joint_state(batch.last_capture_time_us);
    gimbal::JointState joint {};
    (void)topics_.joint_state.read(joint);
    float maximum_joint_speed = 0.0F;
    for (float speed : joint.speed_rad_s) {
        maximum_joint_speed = std::max(
            maximum_joint_speed, std::abs(speed));
    }
    const auto* bytes = static_cast<const std::byte*>(batch.frames);
    gimbal::ImuSample latest_sample {};
    bool estimator_updated = false;
    for (size_t index = 0U; index < batch.frame_count; ++index) {
        const auto* frame = reinterpret_cast<const ImuSensorFrame*>(
            bytes + index * batch.frame_size);
        estimator_updated = process_imu_frame(
            *frame, joint, maximum_joint_speed, latest_sample)
            || estimator_updated;
    }
    (void)topics_.imu_state.publish(latest_sample);
    if (estimator_updated) {
        publish_estimator_state(batch.last_capture_time_us, joint);
    } else {
        motor_enable_requested_.store(false, std::memory_order_release);
        controller_.reset();
        disarm_motors();
    }
    control_tick();
    closed_loop_batches_.fetch_add(1U, std::memory_order_release);
#if defined(CONFIG_APP_WATCHDOG)
    heartbeat_or_panic(WatchdogClient::ClosedLoop);
#endif
}

gimbal::Quaternion GimbalRuntime::planner_target() const
{
    if (!planner_reference_initialized_) {
        return {};
    }
    if (active_mode_ == gimbal::ProductMode::Lock) {
        return lock_target_;
    }
    if (active_mode_ == gimbal::ProductMode::Recenter) {
        return {};
    }
    gimbal::HandleState handle {};
    if (!topics_.handle_state.read(handle)
        || handle.header.health == gimbal::Health::Invalid) {
        return lock_target_;
    }
    const gimbal::Quaternion handle_delta = gimbal::math::multiply(
        gimbal::math::conjugate(handle_origin_),
        handle.handle_in_world);
    gimbal::Vector3 rotation =
        gimbal::math::rotation_vector(handle_delta);
    const float deadband[3] {
        user_.follow_deadband_rad.x,
        user_.follow_deadband_rad.y,
        user_.follow_deadband_rad.z,
    };
    const float gain[3] {
        user_.follow_gain.x, user_.follow_gain.y, user_.follow_gain.z,
    };
    float *components[3] {&rotation.x, &rotation.y, &rotation.z};
    for (size_t axis = 0U; axis < gimbal::kAxisCount; ++axis) {
        if (active_mode_ == gimbal::ProductMode::Follow
            && axis == static_cast<size_t>(gimbal::Axis::Roll)) {
            *components[axis] = 0.0F;
            continue;
        }
        const float value = *components[axis];
        *components[axis] = std::abs(value) <= deadband[axis]
            ? 0.0F : gain[axis] * value;
    }
    return gimbal::math::normalized(gimbal::math::multiply(
        camera_origin_, gimbal::math::from_rotation_vector(rotation)));
}

bool GimbalRuntime::initialize_planner_reference()
{
    if (planner_reference_initialized_ && active_mode_ == user_.mode) {
        return true;
    }
    gimbal::AttitudeState attitude {};
    gimbal::HandleState handle {};
    if (!topics_.attitude_state.read(attitude)
        || !topics_.handle_state.read(handle)
        || attitude.header.health != gimbal::Health::Valid
        || handle.header.health != gimbal::Health::Valid) {
        return false;
    }
    camera_origin_ = attitude.camera_in_world;
    handle_origin_ = handle.handle_in_world;
    lock_target_ = attitude.camera_in_world;
    active_mode_ = user_.mode;
    planner_.reset(attitude.camera_in_world);
    planner_reference_initialized_ = true;
    return true;
}

void GimbalRuntime::planner_tick()
{
    if (!initialize_planner_reference()) {
        return;
    }
    if (!planner_.set_target(planner_target())) {
        return;
    }
    const gimbal::MotionReference reference =
        planner_.update(kPlannerPeriodS, now_us());
    (void)topics_.motion_reference.publish(reference);
}

void GimbalRuntime::disarm_motors()
{
    board::roll_motor().motor().disarm();
    board::pitch_motor().motor().disarm();
    board::yaw_motor().motor().disarm();
    motors_armed_ = false;
}

bool GimbalRuntime::apply_motor_command(
    const gimbal::MotorCommand &command,
    const gimbal::JointState &joint,
    OutputPermission permission)
{
    if (permission != OutputPermission::Allowed || !command.enable) {
        disarm_motors();
        return true;
    }
    if (command.header.health != gimbal::Health::Valid
        || joint.header.health != gimbal::Health::Valid) {
        disarm_motors();
        return false;
    }
    auto &roll = board::roll_motor().motor();
    auto &pitch = board::pitch_motor().motor();
    auto &yaw = board::yaw_motor().motor();
    if (!motors_armed_) {
        if (roll.arm() != hal::Status::Ok
            || pitch.arm() != hal::Status::Ok
            || yaw.arm() != hal::Status::Ok) {
            disarm_motors();
            return false;
        }
        motors_armed_ = true;
    }
    const float bus_voltage = factory_.dynamics.nominal_bus_voltage_v;
    const bool applied = roll.apply(
        joint.electrical_angle_rad[0], command.d_axis_command[0],
        command.q_axis_command[0], bus_voltage) == hal::Status::Ok
        && pitch.apply(
            joint.electrical_angle_rad[1], command.d_axis_command[1],
            command.q_axis_command[1], bus_voltage) == hal::Status::Ok
        && yaw.apply(
            joint.electrical_angle_rad[2], command.d_axis_command[2],
            command.q_axis_command[2], bus_voltage) == hal::Status::Ok;
    if (!applied) {
        disarm_motors();
    }
    return applied;
}

void GimbalRuntime::control_tick()
{
    gimbal::MotionReference reference {};
    gimbal::AttitudeState attitude {};
    gimbal::JointState joint {};
    gimbal::SafetyOutput safety_output {};
    if (!topics_.motion_reference.read(reference)
        || !topics_.attitude_state.read(attitude)
        || !topics_.joint_state.read(joint)) {
        controller_.reset();
        disarm_motors();
        return;
    }
    const uint32_t timestamp = now_us();
    const bool input_stale =
        timestamp - attitude.header.publish_time_us
            > factory_.safety.sensor_timeout_us
        || timestamp - joint.header.publish_time_us
            > factory_.safety.sensor_timeout_us
        || timestamp - reference.header.publish_time_us
            > factory_.safety.command_timeout_us;
    if (input_stale) {
        gimbal::MotorCommand invalid_command = controller_.idle(timestamp);
        invalid_command.header.health = gimbal::Health::Fault;
        (void)topics_.motor_command.publish(invalid_command);
        motor_enable_requested_.store(false, std::memory_order_release);
        disarm_motors();
        return;
    }
    (void)topics_.safety_output.read(safety_output);
    const bool output_allowed = safety_output.allow_motor_output
        && motor_enable_requested_.load(std::memory_order_acquire);
    gimbal::MotorCommand command {};
    if (output_allowed) {
        gimbal::Matrix3 effort_mapping {};
        gimbal::Matrix3 motion_mapping {};
        const bool mapping_valid = kinematics_.calculate_control_mappings(
            joint.angle_rad, effort_mapping, motion_mapping);
        const gimbal::DynamicsOutput forward = mapping_valid
            ? dynamics_.calculate(joint, reference, motion_mapping)
            : gimbal::DynamicsOutput {};
        command = controller_.update({
            reference, attitude, forward, effort_mapping,
            kControlPeriodS, timestamp,
        });
    } else {
        command = controller_.idle(timestamp);
    }
    (void)topics_.motor_command.publish(command);
    const bool motor_output_valid = apply_motor_command(
        command, joint, output_allowed
            ? OutputPermission::Allowed : OutputPermission::Denied);
    if (command.header.health != gimbal::Health::Valid
        || !motor_output_valid) {
        motor_enable_requested_.store(false, std::memory_order_release);
    }
    gimbal::MotorFeedback feedback {};
    feedback.header.sequence = ++feedback_sequence_;
    feedback.header.capture_time_us = now_us();
    feedback.header.publish_time_us = feedback.header.capture_time_us;
    feedback.header.health = kBuildNeedsExternalFeedback
            || !motor_output_valid
        ? gimbal::Health::Fault : gimbal::Health::Valid;
    feedback.header.valid_flags = motor_output_valid ? 0x01U : 0U;
    if (motor_output_valid && motors_armed_) {
        feedback.applied_q_axis = command.q_axis_command;
    }
    (void)topics_.motor_feedback.publish(feedback);
}

void GimbalRuntime::thermal_tick()
{
    gimbal::ImuSample imu_sample {};
    const bool sample_valid = topics_.imu_state.read(imu_sample)
        && imu_sample.header.health == gimbal::Health::Valid
        && std::isfinite(imu_sample.temperature_c);
    gimbal::ThermalOutput output {};
    const bool regulate = factory_valid_
        && factory_.capabilities.has_imu_heater && sample_valid;
    if (regulate) {
        output = thermal_.update(
            imu_sample.temperature_c, kThermalPeriodS);
    }
    const bool heater_write_ok = write_heater_duty(
        regulate ? output.heater_duty : 0.0F);
    gimbal::ImuThermalState state {};
    state.header.sequence = ++thermal_sequence_;
    state.header.capture_time_us = imu_sample.header.capture_time_us;
    state.header.publish_time_us = now_us();
    state.header.health = !heater_write_ok
            || output.mode == gimbal::ThermalMode::Fault
        ? gimbal::Health::Fault
        : (sample_valid ? gimbal::Health::Valid
                        : gimbal::Health::Invalid);
    state.header.valid_flags = sample_valid && heater_write_ok ? 0x01U : 0U;
    state.temperature_c = imu_sample.temperature_c;
    state.heater_duty = heater_write_ok && regulate
        ? output.heater_duty : 0.0F;
    state.is_stable = output.is_ready;
    (void)topics_.imu_thermal_state.publish(state);
}

bool GimbalRuntime::write_heater_duty(float duty)
{
    auto &pwm = board::imu_heater_pwm();
    if (heater_output_fault_) {
        return false;
    }
    if (std::isfinite(duty) && duty >= 0.0F && duty <= 1.0F) {
        const uint32_t pulse = static_cast<uint32_t>(
            duty * board::kHeaterPwmPeriodCounts);
        if (pwm.set_pulse(pulse) == hal::Status::Ok) {
            return true;
        }
    }
    (void)pwm.set_pulse(0U);
    (void)pwm.disable_output();
    (void)pwm.stop();
    heater_output_fault_ = true;
    return false;
}

bool GimbalRuntime::joint_limit_exceeded(
    const gimbal::JointState &joint) const
{
    for (size_t axis = 0U; axis < gimbal::kAxisCount; ++axis) {
        if (joint.angle_rad[axis] < factory_.hall[axis].mechanical_min_rad
            || joint.angle_rad[axis]
                > factory_.hall[axis].mechanical_max_rad) {
            return true;
        }
    }
    return false;
}

bool GimbalRuntime::deadline_missed_since_last_check()
{
    const SensorBatchCore::Diagnostics sensor =
        sensor_core_.diagnostics();
    const uint32_t current[] {
        sensor.trigger_overrun_count,
        sensor.batch_overrun_count,
        sensor.dispatch_error_count,
        sensor.consumer_missed_count,
        planner_task_.missed(), thermal_task_.missed(),
        supervisor_task_.missed(),
    };
    bool missed = false;
    for (size_t index = 0U; index < std::size(current); ++index) {
        missed = missed || current[index] != missed_baseline_[index];
        missed_baseline_[index] = current[index];
    }
    return missed;
}

void GimbalRuntime::supervisor_tick()
{
    gimbal::SafetyInput input {};
    input.now_us = now_us();
    input.factory_parameters_valid = factory_valid_;
    (void)topics_.joint_state.read(input.joint);
    (void)topics_.imu_state.read(input.imu);
    (void)topics_.motor_command.read(input.command);
    (void)topics_.motor_feedback.read(input.feedback);
    gimbal::AttitudeState attitude {};
    (void)topics_.attitude_state.read(attitude);
    input.estimator_valid =
        attitude.header.health == gimbal::Health::Valid;
    input.singular = kinematics_.is_singular(input.joint.angle_rad);
    input.joint_limit_exceeded = joint_limit_exceeded(input.joint);
    const bool deadline_missed = deadline_missed_since_last_check();
    input.deadline_missed =
        safety_.output().state == gimbal::SystemState::Active
        && deadline_missed;
    gimbal::ImuThermalState thermal_state {};
    (void)topics_.imu_thermal_state.read(thermal_state);
    input.heater_ready = !factory_.capabilities.has_imu_heater
        || thermal_state.is_stable;
    input.heater_fault = factory_.capabilities.has_imu_heater
        && thermal_state.header.health == gimbal::Health::Fault;
    const gimbal::SafetyOutput output = safety_.update(
        input, kSupervisorPeriodS);
    if (output.active_faults != 0U || output.latched_faults != 0U
        || output.state == gimbal::SystemState::Calibration
        || output.state == gimbal::SystemState::Fault) {
        motor_enable_requested_.store(false, std::memory_order_release);
    }
    (void)topics_.safety_output.publish(output);
}

gimbal::SafetyOutput GimbalRuntime::safety_status() const
{
    gimbal::SafetyOutput output {};
    (void)topics_.safety_output.read(output);
    return output;
}

bool GimbalRuntime::request_arm()
{
    const gimbal::SafetyOutput output = safety_status();
    if (output.state != gimbal::SystemState::Standby
        || output.active_faults != 0U || output.latched_faults != 0U) {
        return false;
    }
    motor_enable_requested_.store(true, std::memory_order_release);
    safety_.request_arm();
    return true;
}

void GimbalRuntime::request_disarm()
{
    motor_enable_requested_.store(false, std::memory_order_release);
    safety_.request_disarm();
}

void GimbalRuntime::print_diagnostics() const
{
    const gimbal::SafetyOutput safety_output = safety_status();
    LOGI("gimbal", "state=%u active=0x%08lx latched=0x%08lx motor=%u",
         static_cast<unsigned>(safety_output.state),
         static_cast<unsigned long>(safety_output.active_faults),
         static_cast<unsigned long>(safety_output.latched_faults),
         safety_output.allow_motor_output ? 1U : 0U);
    LOGI("gimbal", "first_fault=0x%08lx at=%luus",
         static_cast<unsigned long>(safety_output.first_fault.faults),
         static_cast<unsigned long>(
             safety_output.first_fault.capture_time_us));
    const osal::MemoryStats memory = osal::Kernel::memory_stats();
    LOGI("gimbal", "heap_free=%lu heap_min=%lu",
         static_cast<unsigned long>(memory.free_bytes),
         static_cast<unsigned long>(memory.minimum_free_bytes));
    const SensorBatchCore::Diagnostics sensor =
        sensor_core_.diagnostics();
    const osal::StackStats closed_stack = sensor_core_.stack_stats();
    LOGI("gimbal", "closed stack_free=%lu/%lu missed=%lu",
         static_cast<unsigned long>(closed_stack.minimum_free_bytes),
         static_cast<unsigned long>(closed_stack.total_bytes),
         static_cast<unsigned long>(sensor.consumer_missed_count));
    LOGI("gimbal", "sensor trig=%lu sample=%lu src_err=%lu overrun=%lu",
         static_cast<unsigned long>(sensor.trigger_count),
         static_cast<unsigned long>(sensor.sample_count),
         static_cast<unsigned long>(sensor.source_error_count),
         static_cast<unsigned long>(sensor.trigger_overrun_count));
    LOGI("gimbal", "sensor batch_overrun=%lu dispatch_err=%lu",
         static_cast<unsigned long>(sensor.batch_overrun_count),
         static_cast<unsigned long>(sensor.dispatch_error_count));
    log_task_diagnostics("planner", planner_task_);
    log_task_diagnostics("thermal", thermal_task_);
    log_task_diagnostics("supervisor", supervisor_task_);
    LOGI("gimbal", "drops joint=%lu attitude=%lu handle=%lu",
         static_cast<unsigned long>(
             topics_.joint_state.dropped_publications()),
         static_cast<unsigned long>(
             topics_.attitude_state.dropped_publications()),
         static_cast<unsigned long>(
             topics_.handle_state.dropped_publications()));
    LOGI("gimbal", "drops reference=%lu command=%lu feedback=%lu safety=%lu",
         static_cast<unsigned long>(
             topics_.motion_reference.dropped_publications()),
         static_cast<unsigned long>(
             topics_.motor_command.dropped_publications()),
         static_cast<unsigned long>(
             topics_.motor_feedback.dropped_publications()),
         static_cast<unsigned long>(
             topics_.safety_output.dropped_publications()));
}

void GimbalRuntime::closed_loop_entry(
    void* context, const SensorBatchCore::BatchView& batch)
{
    static_cast<GimbalRuntime*>(context)->closed_loop_batch(batch);
}

void GimbalRuntime::planner_entry(
    void *context, const osal::PeriodicStats &)
{
    static_cast<GimbalRuntime *>(context)->planner_tick();
#if defined(CONFIG_APP_WATCHDOG)
    heartbeat_or_panic(WatchdogClient::Planner);
#endif
}

void GimbalRuntime::thermal_entry(
    void *context, const osal::PeriodicStats &)
{
    static_cast<GimbalRuntime *>(context)->thermal_tick();
#if defined(CONFIG_APP_WATCHDOG)
    heartbeat_or_panic(WatchdogClient::Thermal);
#endif
}

void GimbalRuntime::supervisor_entry(
    void *context, const osal::PeriodicStats &)
{
    static_cast<GimbalRuntime *>(context)->supervisor_tick();
#if defined(CONFIG_APP_WATCHDOG)
    heartbeat_or_panic(WatchdogClient::Supervisor);
#endif
}

} // namespace

int start_gimbal()
{
    return runtime.start();
}

bool wait_gimbal_boot_ready(uint32_t timeout_ms)
{
    const uint32_t start_ms = osal::Kernel::uptime_ms();
    while (osal::Kernel::uptime_ms() - start_ms <= timeout_ms) {
        const gimbal::SafetyOutput output = runtime.safety_status();
        const bool operational_ready =
            (output.state == gimbal::SystemState::Heating
             || output.state == gimbal::SystemState::Standby)
            && output.active_faults == 0U;
        const uint32_t commissioning_faults =
            gimbal::fault_mask(gimbal::Fault::FactoryParameters);
        const bool commissioning_ready =
            output.state == gimbal::SystemState::Calibration
            && (output.active_faults & ~commissioning_faults) == 0U;
        if ((operational_ready || commissioning_ready)
            && output.latched_faults == 0U) {
            return true;
        }
        if (output.state == gimbal::SystemState::Fault) {
            return false;
        }
#if defined(CONFIG_APP_WATCHDOG)
        heartbeat_or_panic(WatchdogClient::Main);
#endif
        osal::this_thread::sleep_for(10U);
    }
    return false;
}

void stop_gimbal()
{
    runtime.stop();
}

bool request_gimbal_arm()
{
    return runtime.request_arm();
}

void request_gimbal_disarm()
{
    runtime.request_disarm();
}

gimbal::SafetyOutput gimbal_safety_status()
{
    return runtime.safety_status();
}

void heartbeat_gimbal_main()
{
#if defined(CONFIG_APP_WATCHDOG)
    heartbeat_or_panic(WatchdogClient::Main);
#endif
}

void print_gimbal_diagnostics()
{
    runtime.print_diagnostics();
}

} // namespace app
