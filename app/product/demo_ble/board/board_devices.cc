#include "board/board_devices.h"

namespace app::board {

decltype(device_get(ble0)) ble() {
    return device_get(ble0);
}

decltype(device_get(ble_uart)) uart() {
    return device_get(ble_uart);
}

decltype(device_get(key0)) hid_key() {
    return device_get(key0);
}

decltype(device_get(led0)) status_led() {
    return device_get(led0);
}

} // namespace app::board
