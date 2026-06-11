#include "services/app_services.h"
#include "control/control_app.h"

#include <log.h>
#include <osal.h>

int main(void) {
    app::confirm_boot_image();

    app::init_logging();

    LOGI("foc", "=== FOC Motor Control Demo (DM-4340) ===");

    app::print_device_registry();

    app::assert_required_devices();

    LOGI("foc", "Type 'help' for CLI commands");

    app::start_control();

    app::start_cli_poll();

    LOGI("foc", "FOC system ready.");

    uint32_t loop_count = 0;
    while (true) {
        osal::this_thread::sleep_for(10);
        loop_count++;
        app::print_periodic_diagnostics(loop_count);
    }
}
