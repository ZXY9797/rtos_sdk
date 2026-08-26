#include "board/board_devices.h"

#include <devicetree.h>
#include <devicetree/gpio.h>

#include <atomic>

extern "C" {
#include "app_drv_error.h"
#include "app_io.h"
#include "soc.h"
}

namespace app::board {

namespace {

constexpr int kHidKeyPin = DT_GPIO_PIN(DT_ALIAS(key0), gpios);
constexpr uint32_t kHidKeyMask = 1UL << kHidKeyPin;
static_assert(kHidKeyPin >= 0 && kHidKeyPin < 16);
static_assert(DT_REG_ADDR(DT_GPIO_CTLR(DT_ALIAS(key0), gpios))
              == GPIO0_BASE);
static_assert(__NVIC_PRIO_BITS > 0U && __NVIC_PRIO_BITS <= 8U);

std::atomic<KeyInterruptCallback> s_key_callback {nullptr};
std::atomic<void *> s_key_context {nullptr};

void hid_key_irq_adapter(app_io_evt_t *event)
{
    if (event == nullptr || event->type != APP_IO_TYPE_GPIOA
        || (event->pin & kHidKeyMask) == 0U) {
        return;
    }
    const KeyInterruptCallback callback =
        s_key_callback.load(std::memory_order_acquire);
    if (callback != nullptr) {
        callback(s_key_context.load(std::memory_order_acquire));
    }
}

} // namespace

decltype(device_get(ble0)) ble() {
    return device_get(ble0);
}

decltype(device_get(uart0)) console() {
    return device_get(uart0);
}

decltype(device_get(ble_uart)) uart() {
    return device_get(ble_uart);
}

decltype(device_get(key0)) hid_key() {
    return device_get(key0);
}

decltype(device_get(led0)) status_led() {
    return device_get(led0);
}

bool enable_hid_key_interrupt(KeyInterruptCallback callback, void *context)
{
    if (callback == nullptr
        || s_key_callback.load(std::memory_order_acquire) != nullptr) {
        return false;
    }

    // app_io enables EXT0 internally. Set a FreeRTOS-callable priority before
    // registration so the ISR may safely release an OSAL semaphore.
    NVIC_SetPriority(
        EXT0_IRQn, static_cast<uint32_t>((1UL << __NVIC_PRIO_BITS) - 1UL));
    s_key_context.store(context, std::memory_order_release);
    s_key_callback.store(callback, std::memory_order_release);

    app_io_init_t config {};
    config.pin = kHidKeyMask;
    config.mode = APP_IO_MODE_IT_BOTH_EDGE;
    config.pull = APP_IO_PULLUP;
    config.mux = APP_IO_MUX;
    if (app_io_event_register_cb(
            APP_IO_TYPE_GPIOA, &config, hid_key_irq_adapter, nullptr)
        != APP_DRV_SUCCESS) {
        s_key_callback.store(nullptr, std::memory_order_release);
        s_key_context.store(nullptr, std::memory_order_release);
        return false;
    }
    return true;
}

bool disable_hid_key_interrupt()
{
    if (s_key_callback.load(std::memory_order_acquire) == nullptr) {
        return true;
    }
    const uint16_t result =
        app_io_event_unregister(APP_IO_TYPE_GPIOA, kHidKeyMask);
    s_key_callback.store(nullptr, std::memory_order_release);
    s_key_context.store(nullptr, std::memory_order_release);
    return result == APP_DRV_SUCCESS;
}

} // namespace app::board
