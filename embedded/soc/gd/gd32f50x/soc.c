#include "soc.h"
#include "gd32f50x_rcu.h"
#include "gd32f50x_gpio.h"

void soc_early_init_hook(void) {
    /* 系统时钟初始化 */
    SystemInit();
}

void soc_late_init_hook(void) {
    /* 后期初始化 */
}
