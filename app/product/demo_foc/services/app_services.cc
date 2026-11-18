#include "services/app_services.h"

#include "board/board_devices.h"
#include "control/control_app.h"
#include "comm/motor_protocol.h"

#include <boot/boot_ctrl.h>
#include <boot/product_info.h>
#include <boot_layout.h>
#include <assert.h>
#include <device.h>
#include <device_base.h>
#include <drivers/uart.h>
#include <drivers_generated.h>
#include <log.h>
#include <init.h>
#include <osal.h>
#include <system/watchdog.h>
#include <arch/arm/cortex_m/fault.h>

#include <cstddef>
#include <cstdint>

namespace {

#if defined(CONFIG_APP_WATCHDOG)
system_watchdog::ClientId g_main_watchdog =
    system_watchdog::kInvalidClient;

int register_main_watchdog()
{
    g_main_watchdog = system_watchdog::register_client(
        "main", CONFIG_APP_WATCHDOG_TIMEOUT_MS);
    return g_main_watchdog != system_watchdog::kInvalidClient ? 0 : -1;
}

SYS_INIT(register_main_watchdog, INITCALL_LEVEL_APPLICATION, 90);
#endif

// 产品信息标记区：app 偏移 1KB 处，用于启动加载器校验防误升级。
__attribute__((section(".product_info"), used))
const boot::ProductInfo kProductInfo = boot::make_product_info(
    boot::layout::kProductId, 0x0100, {1, 0, 0, 0});


void print_uart_stats(hal::UartBase &uart) {
    auto stats = uart.get_stats();
    LOGI("demo_foc", "UART Stats: tx=%lu, rx=%lu, overrun=%lu, fe=%lu, "
         "pe=%lu, tx_timeout=%lu, rx_drop=%lu",
         stats.tx_bytes, stats.rx_bytes,
         stats.overrun_count, stats.framing_errors, stats.parity_errors,
         stats.tx_timeouts, stats.rx_dropped);
}

} // 匿名命名空间

namespace app {

bool confirm_boot_image() {
    return boot::confirm_image();
}

bool init_logging() {
    if (!start_motor_transport()) return false;
    static const LogBackend backend {nullptr, motor_log_backend};
    const LogConfig config {LogLevel::Info, LogBackendId::Custom, &backend};
    return log_init(config) == 0;
}

void print_device_registry() {
    size_t count = 0;
    const auto *registry = hal::get_device_registry(&count);

    LOGI("demo_foc", "--- Device Registry (%zu devices) ---", count);
    for (size_t i = 0; i < count; i++) {
        const bool ready = registry[i].is_ready(registry[i].instance);
        LOGI("demo_foc", "  [%zu] %s (ord=%d, type=%s, ready=%s)",
             i, registry[i].alias, registry[i].ord,
             registry[i].type_name, ready ? "yes" : "no");
    }
    LOGI("demo_foc", "--------------------------------------");
}

void assert_required_devices() {
    HAL_ASSERT(app::board::console().is_initialized());
    HAL_ASSERT(app::board::power_pwm().is_initialized());
    HAL_ASSERT(app::board::current_adc().is_initialized());
    HAL_ASSERT(app::board::joint_adc().is_initialized());
    HAL_ASSERT(app::board::temperature_adc().is_initialized());
}

void print_periodic_diagnostics(uint32_t loop_count) {
#if defined(CONFIG_APP_WATCHDOG)
    if (!system_watchdog::heartbeat(g_main_watchdog)) {
        hal::fault::panic(hal::fault::FatalReason::WatchdogExpired,
                          g_main_watchdog, "main_heartbeat", 0U);
    }
#endif
    if (loop_count % 1000 == 0) {
        print_uart_stats(app::board::console());
    }
}

} // namespace app
