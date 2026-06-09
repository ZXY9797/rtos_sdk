#include "foc_app.h"

#include <device.h>
#include <drivers_generated.h>
#include <log.h>
#include <osal.h>

int main(void) {
    (void)log_uart(device_get(uart0), LogLevel::Info);

    LOGI("foc", "=== FOC Motor Control Demo ===");
    LOGI("foc", "Type 'help' for CLI commands");

    foc_app::start();

    LOGI("foc", "FOC system ready. Motor in IDLE state.");

    while (true) {
        osal::this_thread::sleep_for(10);
    }
}
