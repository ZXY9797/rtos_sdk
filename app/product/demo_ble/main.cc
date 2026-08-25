#include "comm/link_bridge.h"
#include "board/board_devices.h"
#include "services/ble_app.h"
#include "tasks/app_tasks.h"

#include <log.h>

int main(void) {
    if (log_uart(app::board::console(), LogLevel::Info) != 0) {
        return -1;
    }
    LOGI("demo", "GR5525 BLE Demo - HID + UART Transparent");

    if (app::init_ble() != 0) {
        LOGE("demo", "BLE startup failed");
        return -1;
    }
    if (app::init_comm() != 0) {
        LOGE("demo", "communication startup failed");
        return -1;
    }
    if (app::start_app_tasks() != 0) {
        LOGE("demo", "task startup failed");
        app::deinit_comm();
        return -1;
    }
    app::run_heartbeat();
}
