#pragma once

#include <drivers_generated.h>

namespace app::board {

decltype(device_get(ble0)) ble();
decltype(device_get(ble_uart)) uart();
decltype(device_get(key0)) hid_key();
decltype(device_get(led0)) status_led();

} // namespace app::board
