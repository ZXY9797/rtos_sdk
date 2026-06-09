#pragma once

#include "ble_types.h"

namespace ble {

using UartDataCallback = void (*)(const uint8_t *data, size_t len, void *user_data);

class BleUartService {
public:
    Status init(UartDataCallback rx_cb, void *user_data);

    // Send data over BLE UART service
    Status send(uint8_t conn_idx, const uint8_t *data, size_t len);

    // Check if TX notifications are enabled by the peer
    bool is_tx_ready() const;
};

} // namespace ble
