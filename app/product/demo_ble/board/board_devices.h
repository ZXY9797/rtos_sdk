#pragma once

#include <drivers_generated.h>

namespace app::board {

using KeyInterruptCallback = void (*)(void *context);

decltype(device_get(ble0)) ble();
decltype(device_get(uart0)) console();
decltype(device_get(ble_uart)) uart();
decltype(device_get(key0)) hid_key();
decltype(device_get(led0)) status_led();

[[nodiscard]] bool enable_hid_key_interrupt(
    KeyInterruptCallback callback, void *context);
[[nodiscard]] bool disable_hid_key_interrupt();

} // namespace app::board
