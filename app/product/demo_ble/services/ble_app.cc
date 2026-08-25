/**
 * BLE application layer: services, advertising, event handling.
 *
 * This file contains all BLE-specific logic. main.cc only calls
 * init_ble() and the accessor functions.
 */

#include "services/ble_app.h"

#include "board/board_devices.h"
#include "comm/link_bridge.h"

#include <boot/boot_ctrl.h>
#include <boot/product_info.h>
#include <boot_layout.h>
#include <log.h>

#include "ble/ble_device.h"
#include "ble/ble_hid.h"
#include "ble/ble_uart.h"
#include "ble/ble_batt.h"
#include "ble/ble_dis.h"

#include <atomic>

extern "C" {
#include "gr55xx_pwr.h"
}

/* ---- BLE objects ---- */
static std::atomic<ble::BleStack *> s_ble {nullptr};
static ble::BleHidService s_hid;
static ble::BleUartService s_uart;
static ble::BleBattService s_batt;
static ble::BleDisService s_dis;

__attribute__((section(".product_info"), used))
const boot::ProductInfo kProductInfo = boot::make_product_info(
    boot::layout::kProductId, 0x0100, {1, 0, 0, 0});

/* ---- Link layer RX bridge ---- */
static std::atomic<app::BleRxCallback> s_link_rx_cb {nullptr};

static void ble_uart_rx_bridge(const uint8_t *data, size_t len, void *) {
    const app::BleRxCallback callback =
        s_link_rx_cb.load(std::memory_order_acquire);
    if (callback != nullptr) callback(data, len);
}

/* ---- HID Report Map (Keyboard + Consumer Control) ---- */
static const uint8_t s_hid_report_map[] = {
    /* Keyboard */
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /* Report Id 1 */
    0x05, 0x07,       /* Usage Page (Key Codes) */
    0x19, 0xe0,       /* Usage Minimum (224) */
    0x29, 0xe7,       /* Usage Maximum (231) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x08,       /* Report Count (8) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x95, 0x01,       /* Report Count (1) */
    0x75, 0x08,       /* Report Size (8) */
    0x81, 0x01,       /* Input (Constant) */
    0x95, 0x06,       /* Report Count (6) */
    0x75, 0x08,       /* Report Size (8) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x65,       /* Logical Maximum (101) */
    0x05, 0x07,       /* Usage Page (Key Codes) */
    0x19, 0x00,       /* Usage Minimum (0) */
    0x29, 0x65,       /* Usage Maximum (101) */
    0x81, 0x00,       /* Input (Data, Array) */
    0xC0,             /* End Collection */

    /* Consumer Control */
    0x05, 0x0C,       /* Usage Page (Consumer) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x02,       /* Report Id 2 */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x01,       /* Report Count (1) */
    0x09, 0xE9,       /* Usage (Volume Up) */
    0x81, 0x06,
    0x09, 0xEA,       /* Usage (Volume Down) */
    0x81, 0x06,
    0x09, 0xCD,       /* Usage (Play/Pause) */
    0x81, 0x06,
    0xC0
};

/* ---- Advertising data ---- */
static const uint8_t s_adv_data[] = {
    0x0B,
    0x09, /* Complete Local Name */
    'G', 'R', '5', '5', '2', '5', '_', 'D', 'e', 'm', 'o',

    0x03,
    0x19, /* Appearance */
    0xC1, 0x03, /* HID Keyboard */

    0x07,
    0x03, /* Complete List of 16-bit Service UUIDs */
    0x12, 0x18, /* HID Service */
    0x0F, 0x18, /* Battery Service */
    0x0A, 0x18, /* Device Information Service */
};

/* ---- PnP ID ---- */
static const ble::PnpId s_pnp_id = {
    .vendor_id_source = 1,
    .vendor_id        = 0x04F7,
    .product_id       = 0x5525,
    .product_version  = 0x0100
};

/* ---- BLE event callback ---- */
static void on_ble_event(const ble::Event &evt, void *) {
    switch (evt.id) {
    case ble::EventId::StackInit:
        LOGI("ble", "Stack ready, initializing services");
        if (s_dis.init(s_pnp_id) != ble::Status::Ok
            || s_batt.init(100) != ble::Status::Ok
            || s_hid.init_keyboard(
                   {s_hid_report_map, sizeof(s_hid_report_map)})
                   != ble::Status::Ok
            || s_uart.init(ble_uart_rx_bridge, nullptr) != ble::Status::Ok) {
            LOGE("ble", "service initialization failed");
            return;
        }
        if (auto *stack = s_ble.load(std::memory_order_acquire);
            stack == nullptr || stack->adv_start() != ble::Status::Ok) {
            LOGE("ble", "advertising startup failed");
            return;
        }
        // Confirm only after the asynchronous BLE service startup succeeds.
        if (!boot::confirm_image()) {
            LOGE("ble", "image confirmation failed");
            return;
        }
        break;

    case ble::EventId::AdvStarted:
        LOGI("ble", "Advertising started");
        break;

    case ble::EventId::Connected:
        app::set_comm_connected(true);
        LOGI("ble", "Connected to %02X:%02X:%02X:%02X:%02X:%02X",
             evt.peer_addr.addr[5], evt.peer_addr.addr[4],
             evt.peer_addr.addr[3], evt.peer_addr.addr[2],
             evt.peer_addr.addr[1], evt.peer_addr.addr[0]);
        break;

    case ble::EventId::Disconnected:
        app::set_comm_connected(false);
        LOGI("ble", "Disconnected (0x%02X)", evt.disconnect_reason);
        if (auto *stack = s_ble.load(std::memory_order_acquire);
            stack != nullptr) {
            (void)stack->adv_start();
        }
        break;

    case ble::EventId::PairSuccess:
        LOGI("ble", "Pairing succeeded");
        break;

    case ble::EventId::PairFailed:
        LOGW("ble", "Pairing failed (0x%02X)", evt.status);
        break;

    default:
        break;
    }
}

/* ---- Public API ---- */

namespace app {

int init_ble() {
    // BLE config comes from initcall; app supplies the event callback.
    auto &ble_dev = board::ble();
    s_ble.store(&ble_dev.stack(), std::memory_order_release);
    const int result = ble_dev.init(on_ble_event, nullptr);
    if (result != static_cast<int>(ble::Status::Ok)) {
        s_ble.store(nullptr, std::memory_order_release);
        return result != 0 ? result : -1;
    }

    // Application-specific advertising data.
    ble_dev.stack().set_adv_data(s_adv_data, sizeof(s_adv_data));
    return 0;
}

bool ble_is_connected() {
    auto *stack = s_ble.load(std::memory_order_acquire);
    return stack != nullptr && stack->is_connected();
}
uint8_t ble_conn_idx() {
    auto *stack = s_ble.load(std::memory_order_acquire);
    return stack != nullptr ? stack->conn_index() : 0U;
}

void ble_send_keyboard(const uint8_t *report, size_t len) {
    auto *stack = s_ble.load(std::memory_order_acquire);
    if (stack == nullptr) return;
    (void)s_hid.send_keyboard_report(stack->conn_index(), 1U, report, len);
}

bool ble_send_uart(const uint8_t *data, size_t len) {
    auto *stack = s_ble.load(std::memory_order_acquire);
    return stack != nullptr
        && s_uart.send(stack->conn_index(), data, len) == ble::Status::Ok;
}

bool ble_uart_tx_ready() { return s_uart.is_tx_ready(); }

void run_ble_scheduler() { pwr_mgmt_schedule(); }

void set_ble_rx_callback(BleRxCallback cb) {
    s_link_rx_cb.store(cb, std::memory_order_release);
}

} // namespace app
