#pragma once

#include <cstddef>
#include <cstdint>

// Initialize BLE stack + all services. Call from main().
void ble_app_init();

// Check if BLE is connected
bool ble_app_is_connected();

// Get current connection index
uint8_t ble_app_conn_idx();

// Send HID keyboard report
void ble_app_send_keyboard(const uint8_t *report, size_t len);

// Send data over BLE UART service
void ble_app_send_uart(const uint8_t *data, size_t len);

// Check if BLE UART TX is ready
bool ble_app_uart_tx_ready();

// Run BLE scheduler (pwr_mgmt_schedule)
void ble_app_scheduler_run();

// Link 层接入：注册 BLE UART RX 回调
using BleRxCallback = void (*)(const uint8_t *data, size_t len);
void ble_app_set_rx_callback(BleRxCallback cb);
