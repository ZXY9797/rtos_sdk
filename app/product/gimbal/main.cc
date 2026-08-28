#include "board/board_devices.h"
#include "services/gimbal_app.h"

#include <boot/boot_ctrl.h>
#include <boot/product_info.h>
#include <boot_layout.h>
#include <log.h>
#include <osal.h>

namespace {

__attribute__((section(".product_info"), used))
const boot::ProductInfo kProductInfo = boot::make_product_info(
    boot::layout::kProductId, 0x0100, {1, 0, 0, 0});

} // namespace

int main()
{
    if (log_uart(app::board::console(), LogLevel::Info) != 0) {
        return -1;
    }
    LOGI("gimbal", "three-axis gimbal startup");
    if (app::start_gimbal() != 0) {
        LOGE("gimbal", "runtime startup failed; outputs remain disabled");
        return -1;
    }
    if (!app::wait_gimbal_boot_ready(3000U)) {
        LOGE("gimbal", "runtime self-test failed");
        app::stop_gimbal();
        return -1;
    }
    if (!boot::confirm_image()) {
        LOGE("gimbal", "image confirmation failed");
        app::stop_gimbal();
        return -1;
    }
    uint32_t diagnostics_counter = 0U;
    while (true) {
        osal::this_thread::sleep_for(1000U);
        app::heartbeat_gimbal_main();
        if (++diagnostics_counter % 5U == 0U) {
            app::print_gimbal_diagnostics();
        }
    }
}
