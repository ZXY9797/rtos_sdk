#include "services/app_services.h"
#include "control/control_app.h"

#include <log.h>
#include <osal.h>

int main(void) {
    if (!app::init_logging()) {
        return -1;
    }

    LOGI("foc", "=== FOC Motor Control Demo (DM-4340) ===");

    app::print_device_registry();

    app::assert_required_devices();

    LOGI("foc", "Type 'help' for CLI commands");

    if (app::start_control() != 0) {
        LOGE("foc", "control startup failed");
        return -1;
    }

    if (app::start_cli_poll() != 0) {
        LOGE("foc", "CLI startup failed");
        app::stop_control();
        return -1;
    }

    // Confirm only after all mandatory runtime components are healthy.
    if (!app::confirm_boot_image()) {
        LOGE("foc", "image confirmation failed");
        app::stop_cli_poll();
        app::stop_control();
        return -1;
    }
    LOGI("foc", "FOC system ready.");

    uint32_t loop_count = 0;
    while (true) {
        osal::this_thread::sleep_for(10);
        loop_count++;
        app::print_periodic_diagnostics(loop_count);
    }
}
