#include "stddef.h"
#include "devicetree.h"
#include <linker/linker-def.h>

void* arch_early_memset(void* dest, int value, unsigned int n);


void arch_bss_zero(void)
{
	if (IS_ENABLED(CONFIG_SKIP_BSS_CLEAR)) {
		return;
	}

	arch_early_memset(__bss_start, 0, __bss_end - __bss_start);
#if DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(msdk_ccm))
	arch_early_memset(&__ccm_bss_start, 0,
		       (uintptr_t) &__ccm_bss_end
		       - (uintptr_t) &__ccm_bss_start);
#endif
#if DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(msdk_dtcm))
	arch_early_memset(&__dtcm_bss_start, 0,
		       (uintptr_t) &__dtcm_bss_end
		       - (uintptr_t) &__dtcm_bss_start);
#endif
#if DT_NODE_HAS_STATUS_OKAY(DT_CHOSEN(msdk_ocm))
	arch_early_memset(&__ocm_bss_start, 0,
		       (uintptr_t) &__ocm_bss_end
		       - (uintptr_t) &__ocm_bss_start);
#endif
#ifdef CONFIG_CODE_DATA_RELOCATION
	extern void bss_zeroing_relocation(void);

	bss_zeroing_relocation();
#endif	/* CONFIG_CODE_DATA_RELOCATION */
#ifdef CONFIG_COVERAGE_GCOV
	arch_early_memset(&__gcov_bss_start, 0,
		       ((uintptr_t) &__gcov_bss_end - (uintptr_t) &__gcov_bss_start));
#endif /* CONFIG_COVERAGE_GCOV */
#ifdef CONFIG_NOCACHE_MEMORY
	arch_early_memset(&_nocache_ram_start, 0,
			(uintptr_t) &_nocache_ram_end - (uintptr_t) &_nocache_ram_start);
#endif
}