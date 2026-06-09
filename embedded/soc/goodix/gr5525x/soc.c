#include "soc.h"

void soc_early_init_hook(void)
{
    SystemInit();
}

void soc_late_init_hook(void)
{
    /* late init placeholder */
}
