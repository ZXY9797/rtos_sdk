#include "comm/link_bridge.h"
#include "services/ble_app.h"
#include "tasks/app_tasks.h"

#include <log.h>

int main(void) {
    LOGI("demo", "GR5525 BLE Demo - HID + UART Transparent");

    app::init_ble();
    app::init_comm();
    app::start_app_tasks();
    app::run_heartbeat();
}
