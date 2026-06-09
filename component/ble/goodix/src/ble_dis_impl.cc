/**
 * BleDisService implementation for Goodix GR5525.
 *
 * Wraps the GR5525 DIS (Device Information Service) API.
 */

#include "ble/ble_dis.h"

extern "C" {
#include "dis.h"
}

namespace ble {

Status BleDisService::init(const PnpId &pnp_id) {
    dis_pnp_id_t gr_pnp{};
    gr_pnp.vendor_id_source = pnp_id.vendor_id_source;
    gr_pnp.vendor_id = pnp_id.vendor_id;
    gr_pnp.product_id = pnp_id.product_id;
    gr_pnp.product_version = pnp_id.product_version;

    dis_init_t init{};
    init.char_mask = DIS_CHAR_PNP_ID_SUP;
    init.p_pnp_id = &gr_pnp;
    dis_service_init(&init);

    return Status::Ok;
}

} // namespace ble
