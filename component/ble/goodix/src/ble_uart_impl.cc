/**
 * BleUartService implementation for Goodix GR5525.
 *
 * Wraps the GR5525 GUS (Goodix UART Service) for BLE transparent data.
 */

#include "ble/ble_uart.h"
#include "goodix_status.h"

#include <atomic>

extern "C" {
#include "gus.h"
}

namespace ble {

static std::atomic<UartDataCallback> s_rx_cb {nullptr};
static std::atomic<void *> s_rx_user_data {nullptr};
static std::atomic<bool> s_tx_enabled {false};
static std::atomic<bool> s_tx_busy {false};

static void gus_evt_handler(gus_evt_t *p_evt) {
    if (p_evt == nullptr) {
        return;
    }
    switch (p_evt->evt_type) {
    case GUS_EVT_TX_PORT_OPENED:
        s_tx_busy.store(false, std::memory_order_release);
        s_tx_enabled.store(true, std::memory_order_release);
        break;
    case GUS_EVT_TX_PORT_CLOSED:
        s_tx_enabled.store(false, std::memory_order_release);
        s_tx_busy.store(false, std::memory_order_release);
        break;
    case GUS_EVT_RX_DATA_RECEIVED:
        if (const UartDataCallback callback =
                s_rx_cb.load(std::memory_order_acquire);
            callback != nullptr) {
            callback(p_evt->p_data, p_evt->length,
                     s_rx_user_data.load(std::memory_order_acquire));
        }
        break;
    case GUS_EVT_TX_DATA_SENT:
        s_tx_busy.store(false, std::memory_order_release);
        break;
    default:
        break;
    }
}

Status BleUartService::init(UartDataCallback rx_cb, void *user_data) {
    s_tx_enabled.store(false, std::memory_order_release);
    s_tx_busy.store(false, std::memory_order_release);
    s_rx_user_data.store(user_data, std::memory_order_release);
    s_rx_cb.store(rx_cb, std::memory_order_release);

    gus_init_t init{};
    init.evt_handler = gus_evt_handler;
    return goodix::status_from_sdk(gus_service_init(&init));
}

Status BleUartService::send(uint8_t conn_idx, const uint8_t *data, size_t len) {
    if (data == nullptr || len == 0U || len > GUS_MAX_DATA_LEN
        || len > UINT16_MAX) {
        return Status::InvalidParam;
    }
    if (!s_tx_enabled.load(std::memory_order_acquire)) {
        return Status::NotConnected;
    }
    bool expected = false;
    if (!s_tx_busy.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return Status::Busy;
    }
    const sdk_err_t error = gus_tx_data_send(
        conn_idx, const_cast<uint8_t *>(data), static_cast<uint16_t>(len));
    const Status status = goodix::status_from_sdk(error);
    if (status != Status::Ok) {
        s_tx_busy.store(false, std::memory_order_release);
    }
    return status;
}

bool BleUartService::is_tx_ready() const {
    return s_tx_enabled.load(std::memory_order_acquire)
        && !s_tx_busy.load(std::memory_order_acquire);
}

} // namespace ble
