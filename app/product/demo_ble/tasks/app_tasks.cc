#include "tasks/app_tasks.h"

#include "board/board_devices.h"
#include "services/ble_app.h"

#include <drivers/gpio.h>
#include <init.h>
#include <log.h>
#include <osal.h>
#include <system/watchdog.h>

namespace app {
namespace {

osal::Thread s_sched_task;
osal::Thread s_hid_task;
osal::Thread s_uart_task;
alignas(std::max_align_t) uint8_t s_sched_stack[512U] {};
alignas(std::max_align_t) uint8_t s_hid_stack[1024U] {};
alignas(std::max_align_t) uint8_t s_uart_stack[1024U] {};

#if defined(CONFIG_APP_WATCHDOG)
system_watchdog::ClientId s_main_watchdog =
    system_watchdog::kInvalidClient;
system_watchdog::ClientId s_ble_sched_watchdog =
    system_watchdog::kInvalidClient;

int register_watchdog_clients()
{
    s_main_watchdog = system_watchdog::register_client(
        "main", CONFIG_APP_WATCHDOG_TIMEOUT_MS);
    s_ble_sched_watchdog = system_watchdog::register_client(
        "ble_sched", CONFIG_APP_WATCHDOG_TIMEOUT_MS);
    return s_main_watchdog != system_watchdog::kInvalidClient
            && s_ble_sched_watchdog != system_watchdog::kInvalidClient
        ? 0 : -1;
}

SYS_INIT(register_watchdog_clients, INITCALL_LEVEL_APPLICATION, 90);
#endif

bool start_task(osal::Thread &task, const char *name,
                osal::Thread::Entry entry, void *stack, size_t stack_size,
                int32_t priority)
{
    osal::ThreadConfig config {};
    config.name = name;
    config.priority = static_cast<osal::Priority>(priority);
    config.stack_buffer = stack;
    config.stack_size_bytes = stack_size;
    if (task.start(entry, nullptr, config) && task.startup() == 0) {
        return true;
    }
    task.destroy();
    return false;
}

void hid_task_entry(void *) {
    auto &key = board::hid_key();
    auto &led = board::status_led();

    (void)key.configure(GPIO_INPUT | GPIO_PULL_UP);

    static constexpr uint8_t KEY_A = 0x04;
    bool prev_pressed = false;

    while (!s_hid_task.stop_requested()) {
        bool pressed = (key.get() == 0);

        if (pressed && !prev_pressed && ble_is_connected()) {
            uint8_t report[8] = {0, 0, KEY_A, 0, 0, 0, 0, 0};
            ble_send_keyboard(report, sizeof(report));
            led.set();
        } else if (!pressed && prev_pressed && ble_is_connected()) {
            uint8_t report[8] = {0};
            ble_send_keyboard(report, sizeof(report));
            led.clear();
        }

        prev_pressed = pressed;
        osal::this_thread::sleep_for(20);
    }
}

void uart_task_entry(void *) {
    auto &uart = board::uart();

    uint8_t temp[256];
    while (!s_uart_task.stop_requested()) {
        size_t bytes_read = 0;
        if (uart.recv(temp, sizeof(temp), &bytes_read, 10) == hal::Status::Ok
            && bytes_read > 0) {
            if (ble_is_connected() && ble_uart_tx_ready()) {
                (void)ble_send_uart(temp, bytes_read);
            }
        }
        osal::this_thread::sleep_for(1);
    }
}

void ble_sched_task_entry(void *) {
    while (!s_sched_task.stop_requested()) {
#if defined(CONFIG_APP_WATCHDOG)
        if (!system_watchdog::heartbeat(s_ble_sched_watchdog)) {
            return;
        }
#endif
        run_ble_scheduler();
        osal::this_thread::sleep_for(1);
    }
}

} // namespace

int start_app_tasks() {
    if (s_sched_task.is_valid() || s_hid_task.is_valid()
        || s_uart_task.is_valid()) {
        return -1;
    }
    if (!start_task(s_sched_task, "ble_sched", ble_sched_task_entry,
                    s_sched_stack, sizeof(s_sched_stack), 1)) {
        return -1;
    }
    if (!start_task(s_hid_task, "hid", hid_task_entry,
                    s_hid_stack, sizeof(s_hid_stack), 5)) {
        stop_app_tasks();
        return -1;
    }
    if (!start_task(s_uart_task, "uart", uart_task_entry,
                    s_uart_stack, sizeof(s_uart_stack), 4)) {
        stop_app_tasks();
        return -1;
    }
    return 0;
}

void stop_app_tasks() {
    s_uart_task.destroy();
    s_hid_task.destroy();
    s_sched_task.destroy();
}

void run_heartbeat() {
    auto &led = board::status_led();
    (void)led.configure(GPIO_OUTPUT_LOW);

#if defined(CONFIG_APP_WATCHDOG)
    constexpr uint32_t heartbeat_period_ms =
        CONFIG_APP_WATCHDOG_TIMEOUT_MS / 4U;
#else
    constexpr uint32_t heartbeat_period_ms = 2000U;
#endif

    while (true) {
#if defined(CONFIG_APP_WATCHDOG)
        if (!system_watchdog::heartbeat(s_main_watchdog)) {
            return;
        }
#endif
        led.toggle();
        LOGI("demo", "heartbeat");
        osal::this_thread::sleep_for(heartbeat_period_ms);
    }
}

} // namespace app
