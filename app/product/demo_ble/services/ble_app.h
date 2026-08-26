#pragma once

#include <cstddef>
#include <cstdint>

namespace app {

using BleRxCallback = void (*)(const uint8_t *data, size_t len);

struct BleRuntimeStats {
    uint32_t queue_capacity {0U};
    uint32_t queue_depth {0U};
    uint32_t queue_high_water {0U};
    uint32_t queue_send_failures {0U};
    uint32_t event_high_water {0U};
    uint32_t event_dropped {0U};
    uint32_t accepted {0U};
    uint32_t completed {0U};
    uint32_t retries {0U};
    uint32_t dropped {0U};
    uint32_t invalid {0U};
};

int init_ble();
bool ble_is_connected();
[[nodiscard]] bool ble_send_keyboard(const uint8_t *report, size_t len);
[[nodiscard]] bool ble_send_uart(const uint8_t *data, size_t len);
[[nodiscard]] bool process_ble_work(uint32_t wait_ms);
[[nodiscard]] BleRuntimeStats ble_runtime_stats();
void set_ble_rx_callback(BleRxCallback cb);

} // namespace app
