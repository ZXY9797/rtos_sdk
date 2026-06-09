#pragma once

#include "ble_types.h"

namespace ble {

struct HidReportMap {
    const uint8_t *data;
    size_t len;
};

class BleHidService {
public:
    Status init_keyboard(const HidReportMap &report_map);

    // Send keyboard Input Report (modifier + reserved + key[6])
    Status send_keyboard_report(uint8_t conn_idx, uint8_t report_id,
                                const uint8_t *data, size_t len);

    // Send Consumer Control report (volume, play/pause, etc.)
    Status send_consumer_report(uint8_t conn_idx, uint8_t report_id,
                                const uint8_t *data, size_t len);
};

} // namespace ble
