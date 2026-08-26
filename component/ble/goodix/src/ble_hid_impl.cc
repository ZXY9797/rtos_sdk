/**
 * BleHidService implementation for Goodix GR5525.
 *
 * Wraps the GR5525 HIDS (HID Service) API.
 */

#include "ble/ble_hid.h"
#include "goodix_status.h"

#include <atomic>

extern "C" {
#include "hids.h"
#include "bas.h"
#include "dis.h"
#include "app_error.h"
}

namespace ble {

static std::atomic<bool> s_boot_mode {false};

static void hids_evt_handler(hids_evt_t *p_evt) {
    if (p_evt == nullptr) {
        return;
    }
    switch (p_evt->evt_type) {
    case HIDS_EVT_BOOT_MODE_ENTERED:
        s_boot_mode.store(true, std::memory_order_release);
        break;
    case HIDS_EVT_REPORT_MODE_ENTERED:
        s_boot_mode.store(false, std::memory_order_release);
        break;
    default:
        break;
    }
}

Status BleHidService::init_keyboard(const HidReportMap &report_map) {
    if (report_map.data == nullptr || report_map.len == 0U
        || report_map.len > UINT16_MAX) {
        return Status::InvalidParam;
    }
    hids_init_t init{};
    init.evt_handler = hids_evt_handler;
    init.is_kb = true;
    init.is_mouse = false;

    init.hid_info.bcd_hid = 0x0101;
    init.hid_info.b_country_code = 0;
    init.hid_info.flags = HID_INFO_FLAG_REMOTE_WAKE_MSK |
                          HID_INFO_FLAG_NORMALLY_CONNECTABLE_MSK;

    init.report_map.p_map = const_cast<uint8_t *>(report_map.data);
    init.report_map.len = report_map.len;

    // Default: 2 input reports (keyboard + consumer), no output/feature
    init.input_report_count = 2;
    init.input_report_array[0].value_len = 8; // keyboard: modifier+reserved+key[6]
    init.input_report_array[0].ref.report_id = 1;
    init.input_report_array[0].ref.report_type = HIDS_REP_TYPE_INPUT;
    init.input_report_array[1].value_len = 1; // consumer control
    init.input_report_array[1].ref.report_id = 2;
    init.input_report_array[1].ref.report_type = HIDS_REP_TYPE_INPUT;

    init.out_report_sup = false;
    init.feature_report_sup = false;

    return goodix::status_from_sdk(hids_service_init(&init));
}

Status BleHidService::send_keyboard_report(uint8_t conn_idx, uint8_t report_id,
                                           const uint8_t *data, size_t len) {
    if (report_id != 1U || data == nullptr || len != 8U) {
        return Status::InvalidParam;
    }
    sdk_err_t err;
    if (s_boot_mode.load(std::memory_order_acquire)) {
        err = hids_boot_kb_in_rep_send(conn_idx, const_cast<uint8_t *>(data), len);
    } else {
        // report_index 0 = keyboard report
        err = hids_input_rep_send(conn_idx, 0, const_cast<uint8_t *>(data), len);
    }
    return goodix::status_from_sdk(err);
}

Status BleHidService::send_consumer_report(uint8_t conn_idx, uint8_t report_id,
                                           const uint8_t *data, size_t len) {
    if (report_id != 2U || data == nullptr || len != 1U) {
        return Status::InvalidParam;
    }
    // report_index 1 = consumer report
    sdk_err_t err = hids_input_rep_send(conn_idx, 1, const_cast<uint8_t *>(data), len);
    return goodix::status_from_sdk(err);
}

} // namespace ble
