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

static void halt_on_init_error(int error)
{
	(void)error;
	while (1) {
	}
}

static int sys_init_run_level(enum init_level level)
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
			return ret;
		}
	}

	return 0;
}

static void bg_main_thread(void)
{
	int ret = sys_init_run_level(INITCALL_LEVEL_POST_KERNEL);
	if (ret != 0) {
		halt_on_init_error(ret);
	}

	ret = sys_init_run_level(INITCALL_LEVEL_APPLICATION);
	if (ret != 0) {
		halt_on_init_error(ret);
	}

	(void)main();
}

extern "C" void z_cstart(void)
{
	int ret = sys_init_run_level(INITCALL_LEVEL_EARLY);
	if (ret != 0) {
		halt_on_init_error(ret);
	}

	ret = sys_init_run_level(INITCALL_LEVEL_PRE_KERNEL_1);
	if (ret != 0) {
		halt_on_init_error(ret);
	}

	ret = sys_init_run_level(INITCALL_LEVEL_PRE_KERNEL_2);
	if (ret != 0) {
		halt_on_init_error(ret);
	}

	ret = sys_init_run_level(INITCALL_LEVEL_PRE_KERNEL_3);
	if (ret != 0) {
		halt_on_init_error(ret);
	}

	osal_init();
	osal_start(bg_main_thread);

	while (1) {
	}
}
