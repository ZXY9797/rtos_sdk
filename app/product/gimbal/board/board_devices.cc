#include "board/board_devices.h"

namespace app::board {

decltype(device_get(uart0)) console()
{
    return device_get(uart0);
}

decltype(device_get(imu0)) imu()
{
    return device_get(imu0);
}

decltype(device_get(hall_roll)) roll_hall()
{
    return device_get(hall_roll);
}

decltype(device_get(hall_pitch)) pitch_hall()
{
    return device_get(hall_pitch);
}

decltype(device_get(hall_yaw)) yaw_hall()
{
    return device_get(hall_yaw);
}

decltype(device_get(motor_roll)) roll_motor()
{
    return device_get(motor_roll);
}

decltype(device_get(motor_pitch)) pitch_motor()
{
    return device_get(motor_pitch);
}

decltype(device_get(motor_yaw)) yaw_motor()
{
    return device_get(motor_yaw);
}

decltype(device_get(heater_pwm)) imu_heater_pwm()
{
    return device_get(heater_pwm);
}

decltype(device_get(sensor_trigger)) sensor_timer()
{
    return device_get(sensor_trigger);
}

} // namespace app::board
