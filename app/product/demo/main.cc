#include "foc_app.h"
#include "app_services.h"

#include <log.h>
#include <osal.h>

int main(void) {
    demo_app::confirm_boot_image();

    demo_app::init_logging();

    LOGI("foc", "=== FOC Motor Control Demo (DM-4340) ===");

    demo_app::print_device_registry();

    demo_app::assert_required_devices();

    LOGI("foc", "Type 'help' for CLI commands");

    foc_app::start();

    demo_app::start_cli_poll();

    LOGI("foc", "FOC system ready.");

    uint32_t loop_count = 0;
    while (true) {
        osal::this_thread::sleep_for(10);
        loop_count++;
        demo_app::print_periodic_diagnostics(loop_count);
    }
}
