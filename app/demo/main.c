#include "stdint.h"
#include "devicetree.h"
#include "drivers/gpio.h"
#include <osal.h>
#include <sys/cdefs.h>

volatile uint32_t dd_cnt = 128;

#include <irq.h>


/* 通过设备树节点标识符获取GPIO规格 */
#define LED0_NODE DT_ALIAS(led0)
#define BUTTON_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

static struct gpio_callback button_cb_data;

/* 中断回调函数
 * 注意：在此函数中应尽快处理关键操作，避免长时间阻塞
 */
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    /* 通常通过信号量、队列等方式通知其他线程进行后续处理 */
    // k_sem_give(&button_sem);
    gpio_pin_toggle_dt(&led);
}

void dd_test_func(void *parameter)
{
    (void)parameter;
    while(1) {
        osal_sleep(500);
        // gpio_pin_toggle_dt(&led);
        dd_cnt++;
    }
}
osal_thread_t *tid = NULL;
int main() {
    gpio_pin_configure_dt(&led, GPIO_OUTPUT | GPIO_PULL_UP);
    gpio_pin_set_dt(&led, 1);

    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_RISING);

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    tid = osal_thread_create("test_thread", dd_test_func, NULL, 1024, 8, 10);
    if (tid) {
        osal_thread_startup(tid);
    }

    while(1);
    return 0;
}


