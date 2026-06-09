#pragma once

#include "ble_types.h"

namespace ble {

struct StackConfig {
    const char *device_name;
    ConnParam conn_param;
    SecParam sec_param;
    uint16_t adv_interval_min; // in 0.625ms units
    uint16_t adv_interval_max;
    const uint8_t *adv_data;
    size_t adv_data_len;
    const uint8_t *scan_rsp_data;
    size_t scan_rsp_data_len;
};

class BleStack {
public:
    Status init(const StackConfig &cfg, EventCallback cb, void *user_data);

    Status adv_start();
    Status adv_stop();

    bool is_connected() const;
    uint8_t conn_index() const;
    BdAddr peer_addr() const;

    Status conn_param_update(uint8_t conn_idx, const ConnParam &param);
    Status pair_accept(uint8_t conn_idx);
    Status pair_reject(uint8_t conn_idx);
};

} // namespace ble
