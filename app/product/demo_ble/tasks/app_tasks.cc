#include "tasks/app_tasks.h"

#include "board/board_devices.h"
#include "services/ble_app.h"

#include <drivers/gpio.h>
#include <init.h>
#include <log.h>
#include <osal.h>
#include <system/watchdog.h>

#include <atomic>

namespace app {
namespace {

osal::Thread s_ble_owner_task;
alignas(std::max_align_t)
    uint8_t s_ble_owner_stack[CONFIG_BLE_TASK_STACK_SIZE] {};
#if defined(CONFIG_BLE_HID)
osal::Thread s_hid_task;
alignas(std::max_align_t)
    uint8_t s_hid_stack[CONFIG_APP_HID_TASK_STACK_SIZE] {};
osal::Semaphore s_hid_event {0U, 1U};
std::atomic<uint32_t> s_hid_send_failures {0U};
#endif
#if defined(CONFIG_BLE_UART_SERVICE)
osal::Thread s_uart_task;
alignas(std::max_align_t)
    uint8_t s_uart_stack[CONFIG_APP_UART_TASK_STACK_SIZE] {};
uint8_t s_uart_rx_buffer[247U] {};
std::atomic<uint32_t> s_uart_send_failures {0U};
#endif

#if defined(CONFIG_APP_WATCHDOG)
system_watchdog::ClientId s_main_watchdog =
    system_watchdog::kInvalidClient;
system_watchdog::ClientId s_ble_owner_watchdog =
    system_watchdog::kInvalidClient;

int register_watchdog_clients()
{
    s_main_watchdog = system_watchdog::register_client(
        "main", CONFIG_APP_WATCHDOG_TIMEOUT_MS);
    s_ble_owner_watchdog = system_watchdog::register_client(
        "ble_owner", CONFIG_APP_WATCHDOG_TIMEOUT_MS);
    return (s_main_watchdog != system_watchdog::kInvalidClient
            && s_ble_owner_watchdog != system_watchdog::kInvalidClient)
        ? 0
        : -1;
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

#if defined(CONFIG_BLE_HID)
void hid_key_interrupt(void *)
{
    osal::IsrContext context;
    (void)s_hid_event.release_from_isr(context);
}

void hid_task_entry(void *) {
    auto &key = board::hid_key();

    static constexpr uint8_t KEY_A = 0x04;
    bool prev_pressed = (key.get() == 0);

    while (!s_hid_task.stop_requested()) {
        if (s_hid_event.take() != 0 || s_hid_task.stop_requested()) {
            break;
        }
        osal::this_thread::sleep_for(CONFIG_APP_HID_DEBOUNCE_MS);
        if (s_hid_task.stop_requested()) {
            break;
        }
        const bool pressed = (key.get() == 0);
        if (pressed == prev_pressed) {
            continue;
        }

        if (pressed && !prev_pressed && ble_is_connected()) {
            uint8_t report[8] = {0, 0, KEY_A, 0, 0, 0, 0, 0};
            if (!ble_send_keyboard(report, sizeof(report))) {
                s_hid_send_failures.fetch_add(1U,
                                              std::memory_order_relaxed);
            }
        } else if (!pressed && prev_pressed && ble_is_connected()) {
            uint8_t report[8] = {0};
            if (!ble_send_keyboard(report, sizeof(report))) {
                s_hid_send_failures.fetch_add(1U,
                                              std::memory_order_relaxed);
            }
        }

        prev_pressed = pressed;
    }
}
#endif

#if defined(CONFIG_BLE_UART_SERVICE)
void uart_task_entry(void *) {
    auto &uart = board::uart();

    while (!s_uart_task.stop_requested()) {
        size_t bytes_read = 0;
        if (uart.recv(s_uart_rx_buffer, sizeof(s_uart_rx_buffer),
                      &bytes_read, osal::kWaitForever)
                == hal::Status::Ok
            && bytes_read > 0U && ble_is_connected()) {
            if (!ble_send_uart(s_uart_rx_buffer, bytes_read)) {
                s_uart_send_failures.fetch_add(1U,
                                               std::memory_order_relaxed);
            }
        }
    }
}
#endif

void ble_owner_task_entry(void *) {
    while (!s_ble_owner_task.stop_requested()) {
#if defined(CONFIG_APP_WATCHDOG)
        if (!system_watchdog::heartbeat(s_ble_owner_watchdog)) {
            return;
        }
#endif
        (void)process_ble_work(100U);
    }
}

#if defined(CONFIG_APP_RUNTIME_DIAGNOSTICS)
void log_stack_stats(const char *name, const osal::Thread &thread)
{
    const osal::StackStats stats = thread.stack_stats();
    if (stats.available) {
        LOGI("rtos", "stack %s: free_min=%u total=%u", name,
             static_cast<unsigned int>(stats.minimum_free_bytes),
             static_cast<unsigned int>(stats.total_bytes));
    }
}

void log_runtime_diagnostics()
{
    const osal::MemoryStats memory = osal::Kernel::memory_stats();
    if (memory.available) {
        LOGI("rtos", "heap: free=%u free_min=%u",
             static_cast<unsigned int>(memory.free_bytes),
             static_cast<unsigned int>(memory.minimum_free_bytes));
    }
    log_stack_stats("ble_owner", s_ble_owner_task);
#if defined(CONFIG_BLE_HID)
    log_stack_stats("hid", s_hid_task);
#endif
#if defined(CONFIG_BLE_UART_SERVICE)
    log_stack_stats("uart", s_uart_task);
#endif
    const BleRuntimeStats ble_stats = ble_runtime_stats();
    LOGI("ble", "queue=%u/%u high=%u fail=%u evt_high=%u evt_drop=%u in=%u ok=%u retry=%u drop=%u bad=%u",
         static_cast<unsigned int>(ble_stats.queue_depth),
         static_cast<unsigned int>(ble_stats.queue_capacity),
         static_cast<unsigned int>(ble_stats.queue_high_water),
         static_cast<unsigned int>(ble_stats.queue_send_failures),
         static_cast<unsigned int>(ble_stats.event_high_water),
         static_cast<unsigned int>(ble_stats.event_dropped),
         static_cast<unsigned int>(ble_stats.accepted),
         static_cast<unsigned int>(ble_stats.completed),
         static_cast<unsigned int>(ble_stats.retries),
         static_cast<unsigned int>(ble_stats.dropped),
         static_cast<unsigned int>(ble_stats.invalid));
#if defined(CONFIG_BLE_HID)
    LOGI("hid", "enqueue_fail=%u",
         static_cast<unsigned int>(
             s_hid_send_failures.load(std::memory_order_relaxed)));
#endif
#if defined(CONFIG_BLE_UART_SERVICE)
    LOGI("uart", "enqueue_fail=%u",
         static_cast<unsigned int>(
             s_uart_send_failures.load(std::memory_order_relaxed)));
#endif
}
#endif

} // namespace

int start_app_tasks() {
    if (s_ble_owner_task.is_valid()) {
        return -1;
    }
    if (!start_task(s_ble_owner_task, "ble_owner", ble_owner_task_entry,
                    s_ble_owner_stack, sizeof(s_ble_owner_stack),
                    CONFIG_APP_BLE_OWNER_PRIORITY)) {
        return -1;
    }
#if defined(CONFIG_BLE_HID)
    if (s_hid_task.is_valid()
        || !board::enable_hid_key_interrupt(hid_key_interrupt, nullptr)) {
        stop_app_tasks();
        return -1;
    }
    if (!start_task(s_hid_task, "hid", hid_task_entry,
                    s_hid_stack, sizeof(s_hid_stack),
                    CONFIG_APP_HID_TASK_PRIORITY)) {
        stop_app_tasks();
        return -1;
    }
#endif
#if defined(CONFIG_BLE_UART_SERVICE)
    if (s_uart_task.is_valid()) {
        stop_app_tasks();
        return -1;
    }
    if (!start_task(s_uart_task, "uart", uart_task_entry,
                    s_uart_stack, sizeof(s_uart_stack),
                    CONFIG_APP_UART_TASK_PRIORITY)) {
        stop_app_tasks();
        return -1;
    }
#endif
    return 0;
}

void stop_app_tasks() {
#if defined(CONFIG_BLE_HID)
    (void)board::disable_hid_key_interrupt();
#endif
#if defined(CONFIG_BLE_UART_SERVICE)
    s_uart_task.destroy();
#endif
#if defined(CONFIG_BLE_HID)
    s_hid_task.destroy();
#endif
    s_ble_owner_task.destroy();
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

#if defined(CONFIG_APP_RUNTIME_DIAGNOSTICS)
    uint32_t next_diagnostics_ms = osal::Kernel::uptime_ms()
        + CONFIG_APP_RUNTIME_DIAGNOSTICS_INTERVAL_MS;
#endif

    while (true) {
#if defined(CONFIG_APP_WATCHDOG)
        if (!system_watchdog::heartbeat(s_main_watchdog)) {
            return;
        }
#endif
        led.toggle();
        LOGI("demo", "heartbeat");
#if defined(CONFIG_APP_RUNTIME_DIAGNOSTICS)
        const uint32_t now = osal::Kernel::uptime_ms();
        if (static_cast<int32_t>(now - next_diagnostics_ms) >= 0) {
            log_runtime_diagnostics();
            next_diagnostics_ms = now
                + CONFIG_APP_RUNTIME_DIAGNOSTICS_INTERVAL_MS;
        }
#endif
        osal::this_thread::sleep_for(heartbeat_period_ms);
    }
}

} // namespace app
