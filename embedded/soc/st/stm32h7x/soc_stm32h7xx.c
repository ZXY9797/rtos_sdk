#include "soc/soc.h"
#include "system_stm32h7xx.h"
#include "cache.h"
#include <stm32_ll_pwr.h>
#include <stm32_ll_system.h>

void soc_reset_hook(void)
{
    SystemInit();
}

void soc_early_init_hook(void)
{
	sys_cache_instr_enable();
	sys_cache_data_enable();

	/* Update CMSIS SystemCoreClock variable (HCLK) */
	/* At reset, system core clock is set to 64 MHz from HSI */
	SystemCoreClock = 64000000;

	/* Power Configuration */
#if !defined(SMPS) && \
		(defined(CONFIG_POWER_SUPPLY_DIRECT_SMPS) || \
		defined(CONFIG_POWER_SUPPLY_SMPS_1V8_SUPPLIES_LDO) || \
		defined(CONFIG_POWER_SUPPLY_SMPS_2V5_SUPPLIES_LDO) || \
		defined(CONFIG_POWER_SUPPLY_SMPS_1V8_SUPPLIES_EXT_AND_LDO) || \
		defined(CONFIG_POWER_SUPPLY_SMPS_2V5_SUPPLIES_EXT_AND_LDO) || \
		defined(CONFIG_POWER_SUPPLY_SMPS_1V8_SUPPLIES_EXT) || \
		defined(CONFIG_POWER_SUPPLY_SMPS_2V5_SUPPLIES_EXT))
#error Unsupported configuration: Selected SoC do not support SMPS
#endif
#if defined(CONFIG_POWER_SUPPLY_DIRECT_SMPS)
	LL_PWR_ConfigSupply(LL_PWR_DIRECT_SMPS_SUPPLY);
#elif defined(CONFIG_POWER_SUPPLY_SMPS_1V8_SUPPLIES_LDO)
	LL_PWR_ConfigSupply(LL_PWR_SMPS_1V8_SUPPLIES_LDO);
#elif defined(CONFIG_POWER_SUPPLY_SMPS_2V5_SUPPLIES_LDO)
	LL_PWR_ConfigSupply(LL_PWR_SMPS_2V5_SUPPLIES_LDO);
#elif defined(CONFIG_POWER_SUPPLY_SMPS_1V8_SUPPLIES_EXT_AND_LDO)
	LL_PWR_ConfigSupply(LL_PWR_SMPS_1V8_SUPPLIES_EXT_AND_LDO);
#elif defined(CONFIG_POWER_SUPPLY_SMPS_2V5_SUPPLIES_EXT_AND_LDO)
	LL_PWR_ConfigSupply(LL_PWR_SMPS_2V5_SUPPLIES_EXT_AND_LDO);
#elif defined(CONFIG_POWER_SUPPLY_SMPS_1V8_SUPPLIES_EXT)
	LL_PWR_ConfigSupply(LL_PWR_SMPS_1V8_SUPPLIES_EXT);
#elif defined(CONFIG_POWER_SUPPLY_SMPS_2V5_SUPPLIES_EXT)
	LL_PWR_ConfigSupply(LL_PWR_SMPS_2V5_SUPPLIES_EXT);
#elif defined(CONFIG_POWER_SUPPLY_EXTERNAL_SOURCE)
	LL_PWR_ConfigSupply(LL_PWR_EXTERNAL_SOURCE_SUPPLY);
#else
	LL_PWR_ConfigSupply(LL_PWR_LDO_SUPPLY);
#endif
	/* Errata ES0392 Rev 8:
	 * 2.2.9: Reading from AXI SRAM may lead to data read corruption
	 * Workaround: Set the READ_ISS_OVERRIDE bit in the AXI_TARG7_FN_MOD
	 * register.
	 * Applicable only to RevY (REV_ID 0x1003)
	 */
	if (LL_DBGMCU_GetRevisionID() == 0x1003) {
		MODIFY_REG(GPV->AXI_TARG7_FN_MOD, 0x1, 0x1);
	}
}
