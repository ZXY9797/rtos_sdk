/**
 * BleBattService implementation for Goodix GR5525.
 *
 * Wraps the GR5525 BAS (Battery Service) API.
 */

#include "ble/ble_batt.h"
#include "goodix_status.h"

extern "C" {
#include "bas.h"
}

namespace ble {

Status BleBattService::init(uint8_t initial_level) {
    if (initial_level > 100U) {
        return Status::InvalidParam;
    }
    bas_init_t init[1]{};
    init[0].char_mask = BAS_CHAR_MANDATORY | BAS_CHAR_LVL_NTF_SUP;
    init[0].batt_lvl = initial_level;
    init[0].evt_handler = nullptr;
    return goodix::status_from_sdk(bas_service_init(init, 1));
}

Status BleBattService::update_level(uint8_t conn_idx, uint8_t level) {
    if (level > 100U) {
        return Status::InvalidParam;
    }
    return goodix::status_from_sdk(bas_batt_lvl_update(conn_idx, 0, level));
}

} // namespace ble
