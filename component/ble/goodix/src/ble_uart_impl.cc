/**
 * BleUartService implementation for Goodix GR5525.
 *
 * Wraps the GR5525 GUS (Goodix UART Service) for BLE transparent data.
 */

#include "ble/ble_uart.h"

extern "C" {
#include "gus.h"
}

namespace ble {

static volatile UartDataCallback s_rx_cb = nullptr;
static volatile void *s_rx_user_data = nullptr;
static volatile bool s_tx_enabled = false;

static void gus_evt_handler(gus_evt_t *p_evt) {
    switch (p_evt->evt_type) {
    case GUS_EVT_TX_PORT_OPENED:
        s_tx_enabled = true;
        break;
    case GUS_EVT_TX_PORT_CLOSED:
        s_tx_enabled = false;
        break;
    case GUS_EVT_RX_DATA_RECEIVED:
        if (s_rx_cb) {
            s_rx_cb(p_evt->p_data, p_evt->length, const_cast<void *>(static_cast<const volatile void *>(s_rx_user_data)));
        }
        break;
    case GUS_EVT_TX_DATA_SENT:
        break;
    default:
        break;
    }
}

Status BleUartService::init(UartDataCallback rx_cb, void *user_data) {
    s_rx_cb = rx_cb;
    s_rx_user_data = user_data;

    gus_init_t init{};
    init.evt_handler = gus_evt_handler;
    gus_service_init(&init);

    return Status::Ok;
}

Status BleUartService::send(uint8_t conn_idx, const uint8_t *data, size_t len) {
    if (!s_tx_enabled) return Status::NotConnected;
    sdk_err_t err = gus_tx_data_send(conn_idx, const_cast<uint8_t *>(data), len);
    return (err == SDK_SUCCESS) ? Status::Ok : Status::Error;
}

bool BleUartService::is_tx_ready() const { return s_tx_enabled; }

} // namespace ble
