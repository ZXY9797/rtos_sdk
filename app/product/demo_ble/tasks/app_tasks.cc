#include "tasks/app_tasks.h"

#include "board/board_devices.h"
#include "services/ble_app.h"

#include <drivers/gpio.h>
#include <log.h>
#include <osal.h>

namespace app {
namespace {

osal::Thread *s_sched_task = nullptr;
osal::Thread *s_hid_task = nullptr;
osal::Thread *s_uart_task = nullptr;

bool start_task(osal::Thread *&task, const char *name,
                osal::Thread::Entry entry, size_t stack_size,
                int32_t priority)
{
    task = osal::Thread::create(
        name, entry, nullptr, stack_size, priority, 0);
    if (task != nullptr && task->startup() == 0) {
        return true;
    }
    delete task;
    task = nullptr;
    return false;
}

void hid_task_entry(void *) {
    auto &key = board::hid_key();
    auto &led = board::status_led();

    (void)key.configure(GPIO_INPUT | GPIO_PULL_UP);

    static constexpr uint8_t KEY_A = 0x04;
    bool prev_pressed = false;

    while (true) {
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
    while (true) {
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
    while (true) {
        run_ble_scheduler();
        osal::this_thread::sleep_for(1);
    }
}

} // namespace

int start_app_tasks() {
    if (s_sched_task != nullptr || s_hid_task != nullptr
        || s_uart_task != nullptr) {
        return -1;
    }
    if (!start_task(s_sched_task, "ble_sched", ble_sched_task_entry,
                    512U, 1)) {
        return -1;
    }
    if (!start_task(s_hid_task, "hid", hid_task_entry, 1024U, 5)) {
        stop_app_tasks();
        return -1;
    }
    if (!start_task(s_uart_task, "uart", uart_task_entry, 1024U, 4)) {
        stop_app_tasks();
        return -1;
    }
    return 0;
}

void stop_app_tasks() {
    delete s_uart_task;
    s_uart_task = nullptr;
    delete s_hid_task;
    s_hid_task = nullptr;
    delete s_sched_task;
    s_sched_task = nullptr;
}

void run_heartbeat() {
    auto &led = board::status_led();
    (void)led.configure(GPIO_OUTPUT_LOW);

    while (true) {
        led.toggle();
        LOGI("demo", "heartbeat");
        osal::this_thread::sleep_for(2000);
    }
}

} // namespace app
