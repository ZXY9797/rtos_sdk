/**
 * BleDisService implementation for Goodix GR5525.
 *
 * Wraps the GR5525 DIS (Device Information Service) API.
 */

#include "ble/ble_dis.h"
#include "goodix_status.h"

extern "C" {
#include "dis.h"
}

namespace ble {

namespace {

// dis_service_init() retains the p_pnp_id pointer instead of copying the
// pointed-to value, so the converted SDK object must have static lifetime.
dis_pnp_id_t s_pnp_id {};

} // namespace

Status BleDisService::init(const PnpId &pnp_id) {
    s_pnp_id.vendor_id_source = pnp_id.vendor_id_source;
    s_pnp_id.vendor_id = pnp_id.vendor_id;
    s_pnp_id.product_id = pnp_id.product_id;
    s_pnp_id.product_version = pnp_id.product_version;

    dis_init_t init{};
    init.char_mask = DIS_CHAR_PNP_ID_SUP;
    init.p_pnp_id = &s_pnp_id;
    return goodix::status_from_sdk(dis_service_init(&init));
}

} // namespace ble
