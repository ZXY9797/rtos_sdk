#include "board/board_devices.h"
#include <devicetree.h>
#include <devicetree/gpio.h>
#include <drivers/gpio.h>
#include <irq.h>
#include <system/clock.h>

namespace app::board {
namespace {
#define MOTOR_NODE DT_NODELABEL(foc_control)
static_assert(DT_PROP(MOTOR_NODE, timer_clock_hz) == CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
static_assert(DT_PROP_LEN(MOTOR_NODE, current_triggers) == 1 &&
              DT_PROP_LEN(MOTOR_NODE, joint_triggers) == 1);
using CurrentTrigger = hal::DeviceTrait<DT_ORD(DT_PHANDLE(MOTOR_NODE, current_triggers))>
    ::AdcTrigger<DT_PHA(MOTOR_NODE, current_triggers, event)>;
using JointTrigger = hal::DeviceTrait<DT_ORD(DT_PHANDLE(MOTOR_NODE, joint_triggers))>
    ::AdcTrigger<DT_PHA(MOTOR_NODE, joint_triggers, event)>;
static_assert(hal::device_supports<DT_ORD(DT_ALIAS(adc_current)), hal::DeviceCapability::TriggeredScan>
           && hal::device_supports<DT_ORD(DT_ALIAS(adc_joint)), hal::DeviceCapability::TriggeredScan>);
static_assert(hal::device_supports<DT_ORD(DT_ALIAS(can0)), hal::DeviceCapability::CanFd>);
constexpr uint32_t kPeriod = DT_PROP(DT_ALIAS(pwm0), period);
static_assert(kPeriod == DT_PROP(MOTOR_NODE, timer_clock_hz) /
                         (2U * DT_PROP(MOTOR_NODE, pwm_frequency_hz)));
static_assert((DT_PROP(DT_ALIAS(joint_timer), prescaler)+1U) *
              (DT_PROP(DT_ALIAS(joint_timer), period)+1U) * 4000U ==
              DT_PROP(MOTOR_NODE, timer_clock_hz));
alignas(4) uint16_t current_buffer[12] {};
alignas(4) uint16_t joint_buffer[4] {};
bool powered = false;
hal::GpioPort<
    DT_REG_ADDR(DT_GPIO_CTLR(MOTOR_NODE, brake_enable_gpios)),
    DT_GPIO_PIN(MOTOR_NODE, brake_enable_gpios),
    DT_GPIO_FLAGS(MOTOR_NODE, brake_enable_gpios)> brake_enable;
hal::GpioPort<
    DT_REG_ADDR(DT_GPIO_CTLR(MOTOR_NODE, brake_voltage_select_gpios)),
    DT_GPIO_PIN(MOTOR_NODE, brake_voltage_select_gpios),
    DT_GPIO_FLAGS(MOTOR_NODE, brake_voltage_select_gpios)> brake_voltage;
#if defined(CONFIG_DEMO_FOC_MD007_MOTION)
enum class BrakeState : uint8_t { Locked, Releasing, Released };
constexpr uint32_t kBrakeReleaseUs = 300000U;
BrakeState brake_state = BrakeState::Locked;
BrakePhase previous_phase = BrakePhase::Locked;
uint32_t brake_start_us = 0U;

bool prepare_service_brake()
{
    hal::IrqGuard guard;
    const uint32_t now = system_clock::now_us();
    if (brake_state == BrakeState::Locked) {
        brake_voltage.on();
        brake_enable.on();
        brake_start_us = now;
        brake_state = BrakeState::Releasing;
    }
    if (brake_state == BrakeState::Releasing && now - brake_start_us >= kBrakeReleaseUs) {
        brake_voltage.off();
        brake_state = BrakeState::Released;
    }
    return brake_state == BrakeState::Released;
}
#endif
}

decltype(device_get(uart0)) console() { return device_get(uart0); }
decltype(device_get(can0)) main_can() { return device_get(can0); }
decltype(device_get(led0)) status_led() { return device_get(led0); }
decltype(device_get(pwm0)) power_pwm() { return device_get(pwm0); }
decltype(device_get(adc_current)) current_adc() { return device_get(adc_current); }
decltype(device_get(adc_joint)) joint_adc() { return device_get(adc_joint); }
decltype(device_get(adc_temperature)) temperature_adc() { return device_get(adc_temperature); }

bool start_acquisition(hal::AdcBase::ScanCallback current,
                       hal::AdcBase::ScanCallback joint, void* context)
{
    stop_acquisition();
    if (brake_enable.configure(GPIO_OUTPUT_LOW) != 0 ||
        brake_voltage.configure(GPIO_OUTPUT_LOW) != 0) return false;
    hal::AdcScanConfig scan;
    constexpr uint8_t channels[] = DT_PROP(MOTOR_NODE, current_channels);
    static_assert(sizeof(channels) == 6U);
    scan.channel_count = 6U;
    scan.trigger_input = CurrentTrigger::input;
    for (unsigned rank = 0U; rank < 6U; ++rank)
        scan.channels[rank] = {static_cast<hal::AdcChannel>(channels[rank]), hal::AdcSampleTime::Cycles16};
    if (current_adc().start_scan(scan, current_buffer, 12U, current, context) != hal::Status::Ok)
        return false;
    constexpr uint8_t output_channels[] = DT_PROP(MOTOR_NODE, joint_channels);
    static_assert(sizeof(output_channels) == 2U);
    scan.channel_count = 2U;
    scan.trigger_input = JointTrigger::input;
    for (unsigned rank = 0U; rank < 2U; ++rank)
        scan.channels[rank] = {static_cast<hal::AdcChannel>(output_channels[rank]), hal::AdcSampleTime::Cycles16};
    if (joint_adc().start_scan(scan, joint_buffer, 4U, joint, context) != hal::Status::Ok) {
        stop_acquisition(); return false;
    }
    const uint32_t trigger = algo::motor::CurrentLoop::adc_trigger_count(kPeriod,
        DT_PROP(MOTOR_NODE, pwm_frequency_hz), DT_PROP(MOTOR_NODE, adc_before_peak_ns));
    if (CurrentTrigger::configure(power_pwm(), trigger) != hal::Status::Ok ||
        JointTrigger::configure(device_get(joint_timer)) != hal::Status::Ok) {
        stop_acquisition();
        return false;
    }
    if (power_pwm().start() != hal::Status::Ok || device_get(joint_timer).start() != hal::Status::Ok) {
        stop_acquisition(); return false;
    }
    return true;
}

void shutdown()
{
    hal::IrqGuard guard;
    (void)power_pwm().disable_output();
    brake_enable.off();
    brake_voltage.off();
#if defined(CONFIG_DEMO_FOC_MD007_MOTION)
    brake_state = BrakeState::Locked;
    previous_phase = BrakePhase::Locked;
#endif
    powered = false;
}

void stop_acquisition()
{
    shutdown();
    (void)power_pwm().stop();
    (void)device_get(joint_timer).stop();
    (void)current_adc().stop_scan();
    (void)joint_adc().stop_scan();
}

bool apply(const algo::motor::CurrentLoopOutput& output, BrakePhase phase, bool supported_service)
{
    if (!output.valid || !output.energize) {
#if defined(CONFIG_DEMO_FOC_MD007_MOTION)
        if (powered || brake_state != BrakeState::Locked) shutdown();
#else
        if (powered) shutdown();
#endif
        return true;
    }
#if !defined(CONFIG_DEMO_FOC_MD007_MOTION)
    (void)phase;
    (void)supported_service;
    // Commission using acquisition and network feedback while the measured
    // motor limits and brake release timing are being established.
    shutdown();
    return false;
#else
    if (phase == BrakePhase::Locked) {
        if (!supported_service) { shutdown(); return false; }
        if (!prepare_service_brake()) return true;
    }
    using Loop = algo::motor::CurrentLoop;
    if (power_pwm().set_three_phase_pulses(Loop::duty_count(output.duty_percent.u, kPeriod),
        Loop::duty_count(output.duty_percent.v, kPeriod),
        Loop::duty_count(output.duty_percent.w, kPeriod)) != hal::Status::Ok) {
        shutdown(); return false;
    }
    if (!powered) {
        if (power_pwm().enable_output() != hal::Status::Ok) { shutdown(); return false; }
        powered = true;
    }
    if (phase != previous_phase) {
        previous_phase = phase;
        if (phase == BrakePhase::Releasing) {
            brake_voltage.on();
            brake_enable.on();
        } else if (phase == BrakePhase::Running || phase == BrakePhase::Stopping) {
            brake_voltage.off();
            brake_enable.on();
        } else {
            brake_enable.off();
            brake_voltage.off();
        }
    }
    return true;
#endif
}

bool output_enabled() { return powered; }

algo::motor::TemperatureSample temperature(const algo::motor::MotorParameters& parameters)
{
    constexpr uint8_t channels[] = DT_PROP(MOTOR_NODE, temperature_channels);
    static_assert(sizeof(channels) == 3U);
    float voltage[3] {};
    for (unsigned i = 0; i < 3U; ++i) {
        uint16_t raw = 0U;
        const auto channel = static_cast<hal::AdcChannel>(channels[i]);
        if (temperature_adc().set_sample_time(channel, hal::AdcSampleTime::Cycles480) != hal::Status::Ok ||
            temperature_adc().read(channel, raw) != hal::Status::Ok) return {};
        voltage[i] = float(raw) * (3.3F/4095.0F);
    }
    return algo::motor::Protection::sample_temperatures(voltage, 3U,
        parameters.ntc_table, parameters.ntc_count, parameters.temperature.motor_temp_open_voltage_v);
}
} // namespace app::board
