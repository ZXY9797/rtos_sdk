#pragma once

#include "ble_types.h"

namespace ble {

class BleBattService {
public:
    Status init(uint8_t initial_level);
    Status update_level(uint8_t conn_idx, uint8_t level);
};

} // namespace ble
