/**
 * BleBattService implementation for Goodix GR5525.
 *
 * Wraps the GR5525 BAS (Battery Service) API.
 */

#include "ble/ble_batt.h"

extern "C" {
#include "bas.h"
}

namespace ble {

Status BleBattService::init(uint8_t initial_level) {
    bas_init_t init[1]{};
    init[0].char_mask = BAS_CHAR_MANDATORY | BAS_CHAR_LVL_NTF_SUP;
    init[0].batt_lvl = initial_level;
    init[0].evt_handler = nullptr;
    bas_service_init(init, 1);
    return Status::Ok;
}

Status BleBattService::update_level(uint8_t conn_idx, uint8_t level) {
    bas_batt_lvl_update(0, conn_idx, level);
    return Status::Ok;
}

} // namespace ble
