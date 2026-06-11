#include "tasks/app_tasks.h"

#include "board/board_devices.h"
#include "services/ble_app.h"

#include <drivers/gpio.h>
#include <log.h>
#include <osal.h>

namespace app {
namespace {

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
                ble_send_uart(temp, bytes_read);
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

void start_app_tasks() {
    auto *sched_task = osal::Thread::create(
        "ble_sched", ble_sched_task_entry, nullptr, 512, 1, 0);
    if (sched_task) (void)sched_task->startup();

    auto *hid_task = osal::Thread::create(
        "hid", hid_task_entry, nullptr, 1024, 5, 0);
    if (hid_task) (void)hid_task->startup();

    auto *uart_task = osal::Thread::create(
        "uart", uart_task_entry, nullptr, 1024, 4, 0);
    if (uart_task) (void)uart_task->startup();
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
