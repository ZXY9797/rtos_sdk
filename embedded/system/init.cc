#include <init.h>
#include <osal.h>

extern "C" int main(void);

/* Symbols are provided by the linker script in init-level order. */
extern const struct initcall_entry __init_start[];
extern const struct initcall_entry __init_EARLY_start[];
extern const struct initcall_entry __init_PRE_KERNEL_1_start[];
extern const struct initcall_entry __init_PRE_KERNEL_2_start[];
extern const struct initcall_entry __init_PRE_KERNEL_3_start[];
extern const struct initcall_entry __init_POST_KERNEL_start[];
extern const struct initcall_entry __init_APPLICATION_start[];
extern const struct initcall_entry __init_end[];

extern "C" {
volatile const struct initcall_entry *g_init_failed_entry;
volatile int g_init_failed_error;
volatile const struct initcall_entry *g_init_rollback_failed_entry;
volatile int g_init_rollback_failed_error;
}

static void rollback_initialized(const struct initcall_entry *failed)
{
	/* Include the failed initializer: it may have acquired partial state. */
	const struct initcall_entry *entry = failed + 1;
	while (entry > __init_start) {
		--entry;
		if (entry->rollback == nullptr) {
			continue;
		}
		const int error = entry->rollback();
		if (error != 0 && g_init_rollback_failed_entry == nullptr) {
			g_init_rollback_failed_entry = entry;
			g_init_rollback_failed_error = error;
		}
	}
}

static void rollback_before(const struct initcall_entry *end)
{
	const struct initcall_entry *entry = end;
	while (entry > __init_start) {
		--entry;
		if (entry->rollback == nullptr) {
			continue;
		}
		const int error = entry->rollback();
		if (error != 0 && g_init_rollback_failed_entry == nullptr) {
			g_init_rollback_failed_entry = entry;
			g_init_rollback_failed_error = error;
		}
	}
}

static void halt_on_init_error(const struct initcall_entry *entry, int error)
{
	g_init_failed_entry = entry;
	g_init_failed_error = error;
	rollback_initialized(entry);
	__asm volatile("bkpt #0");
	while (1) {
	}
}

static void halt_on_osal_error(int error)
{
	g_init_failed_entry = nullptr;
	g_init_failed_error = error;
	rollback_before(__init_POST_KERNEL_start);
	__asm volatile("bkpt #0");
	while (1) {
	}
}

static const struct initcall_entry *sys_init_run_level(enum init_level level,
                                                       int *error)
{
	static const struct initcall_entry *levels[INITCALL_LEVEL_COUNT + 1] = {
		__init_EARLY_start,
		__init_PRE_KERNEL_1_start,
		__init_PRE_KERNEL_2_start,
		__init_PRE_KERNEL_3_start,
		__init_POST_KERNEL_start,
		__init_APPLICATION_start,
		/* End marker */
		__init_end,
	};

	for (const struct initcall_entry *entry = levels[level];
	     entry < levels[level + 1]; entry++) {
		const int ret = entry->fn();
		if (ret != 0) {
			*error = ret;
			return entry;
		}
	}

	return nullptr;
}

static void bg_main_thread(void)
{
	int error = 0;
	const struct initcall_entry *failed =
		sys_init_run_level(INITCALL_LEVEL_POST_KERNEL, &error);
	if (failed != nullptr) {
		halt_on_init_error(failed, error);
	}

	failed = sys_init_run_level(INITCALL_LEVEL_APPLICATION, &error);
	if (failed != nullptr) {
		halt_on_init_error(failed, error);
	}

	(void)main();
}

extern "C" void z_cstart(void)
{
	int error = 0;
	const struct initcall_entry *failed =
		sys_init_run_level(INITCALL_LEVEL_EARLY, &error);
	if (failed != nullptr) {
		halt_on_init_error(failed, error);
	}

	failed = sys_init_run_level(INITCALL_LEVEL_PRE_KERNEL_1, &error);
	if (failed != nullptr) {
		halt_on_init_error(failed, error);
	}

	failed = sys_init_run_level(INITCALL_LEVEL_PRE_KERNEL_2, &error);
	if (failed != nullptr) {
		halt_on_init_error(failed, error);
	}

	failed = sys_init_run_level(INITCALL_LEVEL_PRE_KERNEL_3, &error);
	if (failed != nullptr) {
		halt_on_init_error(failed, error);
	}

	const int osal_init_error = osal_init();
	if (osal_init_error != 0) {
		halt_on_osal_error(osal_init_error);
	}

	const int osal_start_error = osal_start(bg_main_thread);
	/* A successful scheduler start never returns. */
	halt_on_osal_error(osal_start_error != 0 ? osal_start_error : -1);

	while (1) {
	}
}
