#include "stdint.h"
#include "devicetree.h"
#include "drivers/gpio.h"
#include <osal.h>

volatile uint32_t dd_cnt = 128;

/* 通过设备树节点标识符获取GPIO规格 */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

void dd_test_func()
{
    while(1) {
        osal_sleep(500);
        gpio_pin_toggle_dt(&led);
    }
}
osal_thread_t *tid = NULL;
int main() {
    gpio_pin_configure_dt(&led, GPIO_OUTPUT | GPIO_PULL_UP);
    gpio_pin_set_dt(&led, 1);

    tid = osal_thread_create("test_thread", dd_test_func, NULL, 1024, 8, 10);
    if (tid) {
        osal_thread_startup(tid);
    }
    return 0;
}
