#include "board/board_devices.h"
#include <gd32_regs.h>
#include <devicetree.h>

namespace app::board {
namespace {
#define MOTOR_NODE DT_NODELABEL(foc_control)
constexpr uint32_t kPeriod = DT_PROP(DT_ALIAS(pwm0), period);
static_assert(kPeriod == DT_PROP(MOTOR_NODE, timer_clock_hz) /
                         (2U * DT_PROP(MOTOR_NODE, pwm_frequency_hz)));
static_assert((DT_PROP(DT_ALIAS(joint_timer), prescaler)+1U) *
              (DT_PROP(DT_ALIAS(joint_timer), period)+1U) * 4000U ==
              DT_PROP(MOTOR_NODE, timer_clock_hz));
alignas(4) uint16_t current_buffer[12] {};
alignas(4) uint16_t joint_buffer[4] {};
bool powered = false;
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
    hal::AdcScanConfig scan;
    constexpr uint8_t channels[] = DT_PROP(MOTOR_NODE, current_channels);
    static_assert(sizeof(channels) == 6U);
    scan.channel_count = 6U;
    scan.trigger_input = DT_PROP(MOTOR_NODE, current_trigger_input);
    for (unsigned rank = 0U; rank < 6U; ++rank)
        scan.channels[rank] = {static_cast<hal::AdcChannel>(channels[rank]), hal::AdcSampleTime::Cycles16};
    if (current_adc().start_scan(scan, current_buffer, 12U, current, context) != hal::Status::Ok)
        return false;
    constexpr uint8_t output_channels[] = DT_PROP(MOTOR_NODE, joint_channels);
    static_assert(sizeof(output_channels) == 2U);
    scan.channel_count = 2U;
    scan.trigger_input = DT_PROP(MOTOR_NODE, joint_trigger_input);
    for (unsigned rank = 0U; rank < 2U; ++rank)
        scan.channels[rank] = {static_cast<hal::AdcChannel>(output_channels[rank]), hal::AdcSampleTime::Cycles16};
    if (joint_adc().start_scan(scan, joint_buffer, 4U, joint, context) != hal::Status::Ok) {
        stop_acquisition(); return false;
    }
    const uint32_t pwm_base = DT_REG_ADDR(DT_ALIAS(pwm0));
    const uint32_t trigger = algo::motor::CurrentLoop::adc_trigger_count(kPeriod,
        DT_PROP(MOTOR_NODE, pwm_frequency_hz), DT_PROP(MOTOR_NODE, adc_before_peak_ns));
    auto& pwm = *reinterpret_cast<gd32::TimerRegs*>(pwm_base);
    pwm.CHCTL1 = (pwm.CHCTL1 & ~(7U << 12U)) | (6U << 12U) | (1U << 11U);
    pwm.CHCTL2 |= 1U << 12U;
    if (power_pwm().set_pulse(hal::PwmChannel::Ch4, trigger) != hal::Status::Ok) {
        stop_acquisition(); return false;
    }
    pwm.SWEVG = 1U;
    auto& joint_timer = *reinterpret_cast<gd32::TimerRegs*>(DT_REG_ADDR(DT_ALIAS(joint_timer)));
    joint_timer.CTL1 = (joint_timer.CTL1 & ~(7U << 4U)) | (2U << 4U);
    if (power_pwm().start() != hal::Status::Ok || device_get(joint_timer).start() != hal::Status::Ok) {
        stop_acquisition(); return false;
    }
    return true;
}

void shutdown()
{
    (void)power_pwm().disable_output();
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

bool apply(const algo::motor::CurrentLoopOutput& output)
{
    if (!output.valid || !output.energize) {
        if (powered) shutdown();
        return true;
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
    return true;
}

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
