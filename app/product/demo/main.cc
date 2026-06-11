#include "foc_app.h"

#include <device.h>
#include <device_base.h>
#include <assert.h>
#include <drivers_generated.h>
#include <log.h>
#include <osal.h>
#include <boot/product_info.h>
#include <boot/boot_ctrl.h>

// ProductInfo 标记区: app 偏移 1KB 处，用于 bootloader 校验防误升级
__attribute__((section(".product_info"), used))
const boot::ProductInfo kProductInfo = {
    .magic      = boot::PRODUCT_INFO_MAGIC,
    .product_id = boot::PRODUCT_ID_DEMO,
    .hw_version = 0x0100,       // v1.0
    .fw_version = {1, 0, 0, 0}, // v1.0.0
};

namespace {

constexpr size_t CLI_POLL_STACK = 512;
constexpr int32_t CLI_POLL_PRIO = 8;

/// 打印所有已注册设备信息（演示运行时设备枚举）
void print_device_registry() {
    size_t count = 0;
    const auto *registry = hal::get_device_registry(&count);

    LOGI("demo", "--- Device Registry (%zu devices) ---", count);
    for (size_t i = 0; i < count; i++) {
        LOGI("demo", "  [%zu] %s (ord=%d, type=%s)",
             i, registry[i].alias, registry[i].ord, registry[i].type_name);
    }
    LOGI("demo", "--------------------------------------");
}

/// 打印 UART 统计信息（演示 HAL Stats 诊断）
void print_uart_stats(hal::UartBase &uart) {
    auto stats = uart.get_stats();
    LOGI("demo", "UART Stats: tx=%lu, rx=%lu, overrun=%lu, fe=%lu, pe=%lu",
         stats.tx_bytes, stats.rx_bytes,
         stats.overrun_count, stats.framing_errors, stats.parity_errors);
}

void cli_poll_entry(void *, const osal::PeriodicStats &) {
    auto &uart = device_get(uart0);
    uint8_t ch;
    size_t n = 0;
    while (uart.recv(&ch, 1, &n, 0) == hal::Status::Ok && n > 0) {
        foc_app::feed_char(static_cast<char>(ch));
    }
}

} // namespace

int main(void) {
    // 确认镜像有效，防止 bootloader 回滚
    boot::confirm_image();

    (void)log_uart(device_get(uart0), LogLevel::Info);

    LOGI("foc", "=== FOC Motor Control Demo (DM-4340) ===");

    /* 演示: 运行时设备枚举 */
    print_device_registry();

    /* 演示: 运行时断言 */
    HAL_ASSERT(device_get(uart0).is_initialized());
    HAL_ASSERT(device_get(motor0).is_initialized());

    LOGI("foc", "Type 'help' for CLI commands");

    foc_app::start();

    auto *cli_thread = osal::PeriodicThread::create("cli_poll",
        cli_poll_entry, nullptr,
        CLI_POLL_STACK, CLI_POLL_PRIO,
        100, osal::PeriodicTrigger::Tick);
    if (cli_thread) {
        (void)cli_thread->startup();
    }

    LOGI("foc", "FOC system ready.");

    /* 主循环: 定期打印诊断信息 */
    uint32_t loop_count = 0;
    while (true) {
        osal::this_thread::sleep_for(10);
        loop_count++;

        /* 每 10 秒打印一次 UART 统计 */
        if (loop_count % 1000 == 0) {
            print_uart_stats(device_get(uart0));
        }
    }
}
