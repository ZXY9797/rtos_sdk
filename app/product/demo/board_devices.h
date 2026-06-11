#pragma once

#include <drivers_generated.h>

namespace demo::board {

// 产品级设备门面。硬件别名集中在 board_devices.cc，业务逻辑只使用产品语义名称。
decltype(device_get(uart0)) console();
decltype(device_get(adc0)) main_adc();
#ifdef CONFIG_IMU_ICM40609D
decltype(device_get(imu0)) imu();
#endif
decltype(device_get(led0)) status_led();
decltype(device_get(tim5)) speed_timer();
decltype(device_get(tim6)) control_timer();
decltype(device_get(motor0)) main_motor();

} // namespace demo::board
