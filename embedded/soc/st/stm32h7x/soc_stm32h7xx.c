#include "soc/soc.h"
#include "system_stm32h7xx.h"
#include "arch/cache.h"
#include "devicetree.h"
#include <stm32_ll_pwr.h>

void soc_reset_hook(void)
{
    SystemInit();
}

void soc_early_init_hook(void)
{
#if IS_ENABLED(DT_PROP(DT_NODELABEL(cpu0), i_cache_enabled))
    arch_icache_enable();
#endif
#if IS_ENABLED(DT_PROP(DT_NODELABEL(cpu0), d_cache_enabled))
    arch_dcache_enable();
#endif
	/* Update CMSIS SystemCoreClock variable (HCLK) */
	/* At reset, system core clock is set to 64 MHz from HSI */
	SystemCoreClock = 64000000;

    LL_PWR_ConfigSupply(LL_PWR_LDO_SUPPLY);
}
