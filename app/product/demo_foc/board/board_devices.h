#pragma once
#include <drivers_generated.h>
#include <algo/motor.h>

namespace app::board {
decltype(device_get(uart0)) console();
decltype(device_get(can0)) main_can();
decltype(device_get(led0)) status_led();
decltype(device_get(pwm0)) power_pwm();
decltype(device_get(adc_current)) current_adc();
decltype(device_get(adc_joint)) joint_adc();
decltype(device_get(adc_temperature)) temperature_adc();
bool start_acquisition(hal::AdcBase::ScanCallback current,
                       hal::AdcBase::ScanCallback joint, void* context);
void stop_acquisition();
bool apply(const algo::motor::CurrentLoopOutput& output);
void shutdown();
algo::motor::TemperatureSample temperature(const algo::motor::MotorParameters& parameters);
} // namespace app::board
