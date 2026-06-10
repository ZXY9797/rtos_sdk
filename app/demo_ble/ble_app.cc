/**
 * BLE application layer — services, advertising, event handling.
 *
 * This file contains all BLE-specific logic. main.cc only calls
 * ble_app_init() and the accessor functions.
 */

#include "ble_app.h"

#include <device.h>
#include <drivers_generated.h>
#include <log.h>

#include "ble/ble_device.h"
#include "ble/ble_hid.h"
#include "ble/ble_uart.h"
#include "ble/ble_batt.h"
#include "ble/ble_dis.h"

extern "C" {
#include "gr55xx_pwr.h"
}

/* ---- BLE objects ---- */
static ble::BleStack *s_ble = nullptr;
static ble::BleHidService s_hid;
static ble::BleUartService s_uart;
static ble::BleBattService s_batt;
static ble::BleDisService s_dis;

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
        s_dis.init(s_pnp_id);
        s_batt.init(100);
        s_hid.init_keyboard({s_hid_report_map, sizeof(s_hid_report_map)});
        s_uart.init(nullptr, nullptr);
        s_ble->adv_start();
        break;

    case ble::EventId::AdvStarted:
        LOGI("ble", "Advertising started");
        break;

    case ble::EventId::Connected:
        LOGI("ble", "Connected to %02X:%02X:%02X:%02X:%02X:%02X",
             evt.peer_addr.addr[5], evt.peer_addr.addr[4],
             evt.peer_addr.addr[3], evt.peer_addr.addr[2],
             evt.peer_addr.addr[1], evt.peer_addr.addr[0]);
        break;

    case ble::EventId::Disconnected:
        LOGI("ble", "Disconnected (0x%02X)", evt.disconnect_reason);
        s_ble->adv_start();
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

void ble_app_init() {
    // BLE 配置由设备树 initcall 自动存储，应用层只需提供事件回调
    auto &ble_dev = device_get(ble0);
    ble_dev.init(on_ble_event, nullptr);
    s_ble = &ble_dev.stack();

    // 应用层特定：设置广播数据
    s_ble->set_adv_data(s_adv_data, sizeof(s_adv_data));
}

bool ble_app_is_connected() { return s_ble->is_connected(); }
uint8_t ble_app_conn_idx() { return s_ble->conn_index(); }

void ble_app_send_keyboard(const uint8_t *report, size_t len) {
    s_hid.send_keyboard_report(s_ble->conn_index(), 1, report, len);
}

void ble_app_send_uart(const uint8_t *data, size_t len) {
    s_uart.send(s_ble->conn_index(), data, len);
}

bool ble_app_uart_tx_ready() { return s_uart.is_tx_ready(); }

void ble_app_scheduler_run() { pwr_mgmt_schedule(); }
