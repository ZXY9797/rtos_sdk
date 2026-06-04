#include <linker/linker-def.h>
#include <devicetree.h>

void* arch_early_memcpy(void* dest, const void* src, unsigned int n);

/**
 * @brief Copy the data section from ROM to RAM
 *
 * This routine copies the data section from ROM to RAM.
 */
void arch_data_copy(void)
{
	arch_early_memcpy(&__data_region_start, &__data_region_load_start,
		       __data_region_end - __data_region_start);
#ifdef CONFIG_ARCH_HAS_RAMFUNC_SUPPORT
	arch_early_memcpy(&__ramfunc_region_start, &__ramfunc_load_start,
		       __ramfunc_end - __ramfunc_region_start);
#endif /* CONFIG_ARCH_HAS_RAMFUNC_SUPPORT */

#ifdef CONFIG_ARCH_HAS_NOCACHE_MEMORY_SUPPORT
#if CONFIG_NOCACHE_MEMORY
	arch_early_memcpy(&_nocache_load_ram_start, &_nocache_load_rom_start,
		       (uintptr_t) &_nocache_load_ram_size);
#endif /* CONFIG_NOCACHE_MEMORY */
#endif /* CONFIG_ARCH_HAS_NOCACHE_MEMORY_SUPPORT */

#if DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(rtos_sdk_ccm))
	arch_early_memcpy(&__ccm_data_start, &__ccm_data_load_start,
		       __ccm_data_end - __ccm_data_start);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(rtos_sdk_itcm))
	arch_early_memcpy(&__itcm_start, &__itcm_load_start,
		       (uintptr_t) &__itcm_size);
#endif

#if DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(rtos_sdk_dtcm))
	arch_early_memcpy(&__dtcm_data_start, &__dtcm_data_load_start,
		       __dtcm_data_end - __dtcm_data_start);
#endif

#ifdef CONFIG_CODE_DATA_RELOCATION
	extern void data_copy_xip_relocation(void);

	data_copy_xip_relocation();
#endif	/* CONFIG_CODE_DATA_RELOCATION */

}