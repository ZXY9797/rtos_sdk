#include "board_devices.h"

namespace demo::board {

decltype(device_get(uart0)) console() {
    return device_get(uart0);
}

decltype(device_get(adc0)) main_adc() {
    return device_get(adc0);
}

#ifdef CONFIG_IMU_ICM40609D
decltype(device_get(imu0)) imu() {
    return device_get(imu0);
}
#endif

decltype(device_get(led0)) status_led() {
    return device_get(led0);
}

decltype(device_get(tim5)) speed_timer() {
    return device_get(tim5);
}

decltype(device_get(tim6)) control_timer() {
    return device_get(tim6);
}

decltype(device_get(motor0)) main_motor() {
    return device_get(motor0);
}

} // namespace demo::board
