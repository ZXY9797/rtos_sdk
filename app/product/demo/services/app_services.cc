#include "services/app_services.h"

#include "board/board_devices.h"
#include "control/control_app.h"

#include <boot/boot_ctrl.h>
#include <boot/product_info.h>
#include <boot_layout.h>
#include <assert.h>
#include <device.h>
#include <device_base.h>
#include <drivers/uart.h>
#include <drivers_generated.h>
#include <log.h>
#include <osal.h>

#include <cstddef>
#include <cstdint>

namespace {

constexpr size_t CLI_POLL_STACK = 512;
constexpr int32_t CLI_POLL_PRIO = 8;
osal::PeriodicThread *g_cli_thread = nullptr;

// 产品信息标记区：app 偏移 1KB 处，用于启动加载器校验防误升级。
__attribute__((section(".product_info"), used))
const boot::ProductInfo kProductInfo = boot::make_product_info(
    boot::layout::kProductId, 0x0100, {1, 0, 0, 0});

void cli_poll_entry(void *, const osal::PeriodicStats &) {
    auto &uart = app::board::console();
    uint8_t ch;
    size_t n = 0;
    while (uart.recv(&ch, 1, &n, 0) == hal::Status::Ok && n > 0) {
        app::feed_control_char(static_cast<char>(ch));
    }
}

void print_uart_stats(hal::UartBase &uart) {
    auto stats = uart.get_stats();
    LOGI("demo", "UART Stats: tx=%lu, rx=%lu, overrun=%lu, fe=%lu, "
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
    return log_uart(app::board::console(), LogLevel::Info) == 0;
}

void print_device_registry() {
    size_t count = 0;
    const auto *registry = hal::get_device_registry(&count);

    LOGI("demo", "--- Device Registry (%zu devices) ---", count);
    for (size_t i = 0; i < count; i++) {
        const bool ready = registry[i].is_ready(registry[i].instance);
        LOGI("demo", "  [%zu] %s (ord=%d, type=%s, ready=%s)",
             i, registry[i].alias, registry[i].ord,
             registry[i].type_name, ready ? "yes" : "no");
    }
    LOGI("demo", "--------------------------------------");
}

void assert_required_devices() {
    HAL_ASSERT(app::board::console().is_initialized());
    HAL_ASSERT(app::board::main_motor().is_initialized());
}

int start_cli_poll() {
    if (g_cli_thread != nullptr) return 0;
    g_cli_thread = osal::PeriodicThread::create("cli_poll",
        cli_poll_entry, nullptr,
        CLI_POLL_STACK, CLI_POLL_PRIO,
        100, osal::PeriodicTrigger::Tick);
    if (g_cli_thread == nullptr) return -1;
    if (g_cli_thread->startup() != 0) {
        delete g_cli_thread;
        g_cli_thread = nullptr;
        return -1;
    }
    return 0;
}

void stop_cli_poll() {
    delete g_cli_thread;
    g_cli_thread = nullptr;
}

void print_periodic_diagnostics(uint32_t loop_count) {
    if (loop_count % 1000 == 0) {
        print_uart_stats(app::board::console());
    }
}

} // namespace app
