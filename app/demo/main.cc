#include <device.h>
#include <drivers/gpio.h>
#include <drivers_generated.h>
#include <init.h>
#include <log.h>
#include <osal.h>

#include <cstddef>
#include <cstdint>

namespace {

constexpr uint32_t PERIODIC_HZ = 10;
constexpr size_t PERIODIC_STACK_SIZE = 1024;
constexpr int32_t PERIODIC_PRIORITY = 8;

volatile bool g_gpio_initcall_ran = false;
volatile bool g_app_initcall_ran = false;

osal::PeriodicThread *g_periodic = nullptr;

int demo_gpio_init()
{
    auto &led = device_get(led0);
    auto &key = device_get(key0);

    (void)led.configure(GPIO_OUTPUT_LOW);
    (void)key.configure(GPIO_INPUT | GPIO_PULL_UP);
    led.off();

    g_gpio_initcall_ran = true;
    return 0;
}

int demo_application_init()
{
    g_app_initcall_ran = true;
    return 0;
}

void periodic_entry(void *, const osal::PeriodicStats &stats)
{
    auto &led = device_get(led0);
    auto &key = device_get(key0);

    static bool last_pressed = false;
    const bool pressed = key.is_on();

    if (pressed) {
        led.on();
    } else if ((stats.sequence % (PERIODIC_HZ / 2)) == 0) {
        led.toggle();
    }

    if (pressed != last_pressed) {
        last_pressed = pressed;
        LOGI("demo", "key0 %s, seq=%lu, missed=%lu",
             pressed ? "pressed" : "released",
             static_cast<unsigned long>(stats.sequence),
             static_cast<unsigned long>(stats.missed));
    }

    if ((stats.sequence % PERIODIC_HZ) == 0) {
        LOGI("demo", "PeriodicThread heartbeat, seq=%lu, key=%u, missed=%lu",
             static_cast<unsigned long>(stats.sequence),
             pressed ? 1U : 0U,
             static_cast<unsigned long>(stats.missed));
    }
}

} // namespace

SYS_INIT(demo_gpio_init, INITCALL_LEVEL_PRE_KERNEL_3, 80);
SYS_INIT(demo_application_init, INITCALL_LEVEL_APPLICATION, 80);

int main(void)
{
    (void)log_uart(device_get(uart0), LogLevel::Info);

    LOGI("demo", "rtos_sdk demo: initcall + key + led + uart console + PeriodicThread");
    LOGI("demo", "LED blinks at 1Hz, key0 forces LED on and logs edge changes");
    LOGI("demo", "initcall flags: gpio=%u, app=%u",
         g_gpio_initcall_ran ? 1U : 0U,
         g_app_initcall_ran ? 1U : 0U);

    g_periodic = osal::PeriodicThread::create("demo_per",
                                              periodic_entry,
                                              nullptr,
                                              PERIODIC_STACK_SIZE,
                                              PERIODIC_PRIORITY,
                                              PERIODIC_HZ,
                                              osal::PeriodicTrigger::Tick);
    if (!g_periodic || g_periodic->startup() != 0) {
        LOGE("demo", "failed to start PeriodicThread");
        while (true) {
            osal::this_thread::sleep_for(1000);
        }
    }

    LOGI("demo", "PeriodicThread started at %lu Hz",
         static_cast<unsigned long>(PERIODIC_HZ));

    while (true) {
        osal::this_thread::sleep_for(1000);
    }
}
