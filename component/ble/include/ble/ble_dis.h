#pragma once

#include "ble_types.h"

namespace ble {

struct PnpId {
    uint8_t vendor_id_source;
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t product_version;
};

class BleDisService {
public:
    Status init(const PnpId &pnp_id);
};

} // namespace ble
