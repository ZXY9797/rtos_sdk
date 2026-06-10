/**
 * GR5525 BLE Demo — HID Keyboard + UART Transparent Pass-through
 *
 * Uses vendor-agnostic ble:: API. Business logic is portable across
 * BLE chip vendors (Goodix, Nordic, ST, etc.).
 */

#include <device.h>
#include <drivers/gpio.h>
#include <drivers/uart.h>
#include <drivers_generated.h>
#include <init.h>
#include <log.h>
#include <osal.h>

#ifdef CONFIG_LINK
#include <link/router.h>
#include <link/ble_link.h>
#endif

#include "ble_app.h"

/* ==================================================================
 * Link 层接入
 * ================================================================== */

#ifdef CONFIG_LINK
static link::BleLink g_ble_link(20);  // BLE 4.x: frag_payload=20

static void ble_link_tx(const uint8_t *data, size_t len, void *) {
    ble_app_send_uart(data, len);
}

static void ble_link_rx(const uint8_t *data, size_t len) {
    g_ble_link.on_receive(data, len);
}

static void comm_init() {
    g_ble_link.set_id(1);
    g_ble_link.set_tx_func(ble_link_tx, nullptr);
    g_ble_link.set_connected(ble_app_is_connected());
    ble_app_set_rx_callback(ble_link_rx);

    auto &router = link::Router::instance();
    router.set_self_addr(link::make_addr(0x20, 0));  // BLE 设备 host_id=0x20

    static const link::RouteEntry routes[] = {
        link::make_route(link::route_by_host(0x10, 0xF0).to(1)),
        link::make_route(link::route_direct(0).to(1)),
    };
    router.set_routes(routes, 2);

    LOGI("link", "BLE comm initialized: self=0x%02x", link::make_addr(0x20, 0));
}
#endif

/* ==================================================================
 * HID Keyboard Task
 * ================================================================== */

static void hid_task_entry(void *param) {
    (void)param;
    auto &key = device_get(key0);
    auto &led = device_get(led0);

    (void)key.configure(GPIO_INPUT | GPIO_PULL_UP);

    static const uint8_t KEY_A = 0x04;
    bool prev_pressed = false;

    while (true) {
        bool pressed = (key.get() == 0); /* ACTIVE_LOW */

        if (pressed && !prev_pressed && ble_app_is_connected()) {
            uint8_t report[8] = {0, 0, KEY_A, 0, 0, 0, 0, 0};
            ble_app_send_keyboard(report, sizeof(report));
            led.set();
        } else if (!pressed && prev_pressed && ble_app_is_connected()) {
            uint8_t report[8] = {0};
            ble_app_send_keyboard(report, sizeof(report));
            led.clear();
        }

        prev_pressed = pressed;
        osal::this_thread::sleep_for(20);
    }
}

/* ==================================================================
 * UART Transparent Pass-through Task
 * ================================================================== */

static void uart_task_entry(void *param) {
    (void)param;
    auto &uart = device_get(ble_uart);

    uint8_t temp[256];
    while (true) {
        size_t bytes_read = 0;
        if (uart.recv(temp, sizeof(temp), &bytes_read, 10) == hal::Status::Ok
            && bytes_read > 0) {
            if (ble_app_is_connected() && ble_app_uart_tx_ready()) {
                ble_app_send_uart(temp, bytes_read);
            }
        }
        osal::this_thread::sleep_for(1);
    }
}

/* ==================================================================
 * BLE Scheduler Task (pwr_mgmt_schedule)
 * ================================================================== */

static void ble_sched_task_entry(void *param) {
    (void)param;
    while (true) {
        ble_app_scheduler_run();
        osal::this_thread::sleep_for(1);
    }
}

/* ==================================================================
 * Main Entry
 * ================================================================== */

int main(void) {
    LOGI("demo", "GR5525 BLE Demo — HID + UART Transparent");

    /* Initialize BLE stack + all services */
    ble_app_init();

#ifdef CONFIG_LINK
    comm_init();
#endif

    /* BLE scheduler task */
    auto *sched_task = osal::Thread::create(
        "ble_sched", ble_sched_task_entry, nullptr, 512, 1, 0);
    if (sched_task) (void)sched_task->startup();

    /* HID keyboard task */
    auto *hid_task = osal::Thread::create(
        "hid", hid_task_entry, nullptr, 1024, 5, 0);
    if (hid_task) (void)hid_task->startup();

    /* UART transparent task */
    auto *uart_task = osal::Thread::create(
        "uart", uart_task_entry, nullptr, 1024, 4, 0);
    if (uart_task) (void)uart_task->startup();

    /* Main task: LED heartbeat */
    auto &led = device_get(led0);
    (void)led.configure(GPIO_OUTPUT_LOW);

    while (true) {
        led.toggle();
        LOGI("demo", "heartbeat");
        osal::this_thread::sleep_for(2000);
    }
}
