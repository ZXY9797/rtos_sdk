#include "stdint.h"
#include "devicetree.h"
#include "drivers/gpio.h"
#include <zephyr/kernel.h>
#include <sys/cdefs.h>

volatile uint32_t dd_cnt = 128;

#include <irq.h>


/* 通过设备树节点标识符获取GPIO规格 */
#define LED0_NODE DT_ALIAS(led0)
#define BUTTON_NODE DT_ALIAS(sw0)

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(BUTTON_NODE, gpios);

static struct gpio_callback button_cb_data;

/* 线程栈定义 */
K_THREAD_STACK_DEFINE(test_thread_stack, 1024);
static struct k_thread test_thread_data;

/* 中断回调函数
 * 注意：在此函数中应尽快处理关键操作，避免长时间阻塞
 */
void button_pressed(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    /* 通常通过信号量、队列等方式通知其他线程进行后续处理 */
    // k_sem_give(&button_sem);
    gpio_pin_toggle_dt(&led);
}

void dd_test_func(void *p1, void *p2, void *p3)
{
    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while(1) {
        k_msleep(500);
        // gpio_pin_toggle_dt(&led);
        dd_cnt++;
    }
}

int main() {
    k_tid_t tid;

    gpio_pin_configure_dt(&led, GPIO_OUTPUT | GPIO_PULL_UP);
    gpio_pin_set_dt(&led, 1);

    gpio_pin_configure_dt(&button, GPIO_INPUT | GPIO_PULL_DOWN);
    gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_RISING);

    gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
    gpio_add_callback(button.port, &button_cb_data);

    /* 创建线程 */
    tid = k_thread_create(&test_thread_data, test_thread_stack,
                          K_THREAD_STACK_SIZEOF(test_thread_stack),
                          dd_test_func, NULL, NULL, NULL,
                          8, 0, K_NO_WAIT);

    if (tid) {
        /* 线程已经在k_thread_create中启动（delay=K_NO_WAIT） */
    }

    while(1);
    return 0;
}
