#pragma once

#include <cstddef>
#include <cstdint>

namespace app {

using BleRxCallback = void (*)(const uint8_t *data, size_t len);

void init_ble();
bool ble_is_connected();
uint8_t ble_conn_idx();
void ble_send_keyboard(const uint8_t *report, size_t len);
void ble_send_uart(const uint8_t *data, size_t len);
bool ble_uart_tx_ready();
void run_ble_scheduler();
void set_ble_rx_callback(BleRxCallback cb);

} // namespace app
