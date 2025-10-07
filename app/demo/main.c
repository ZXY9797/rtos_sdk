#include "stdint.h"
#include "devicetree.h"
#include "drivers/gpio.h"

volatile uint32_t dd_cnt = 128;

/* 通过设备树节点标识符获取GPIO规格 */
#define LED0_NODE DT_ALIAS(led0)
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

void dd_test_func()
{
    gpio_pin_toggle_dt(&led);
}
int main() {
    gpio_pin_configure_dt(&led, GPIO_OUTPUT | GPIO_PULL_UP);
    gpio_pin_set_dt(&led, 1);
    while(1) {
        // if (dd_cnt == 6000000) {
        //     dd_cnt = 0;
        //     gpio_pin_toggle_dt(&led);
        // }
        dd_cnt++;
    }
    return 0;
}
