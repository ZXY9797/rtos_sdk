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

static void sys_init_run_level(enum init_level level)
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
		entry->fn();
	}
}

static void bg_main_thread(void)
{
	sys_init_run_level(INITCALL_LEVEL_POST_KERNEL);
	sys_init_run_level(INITCALL_LEVEL_APPLICATION);
	main();
}

extern "C" void z_cstart(void)
{
	sys_init_run_level(INITCALL_LEVEL_EARLY);
	sys_init_run_level(INITCALL_LEVEL_PRE_KERNEL_1);
	sys_init_run_level(INITCALL_LEVEL_PRE_KERNEL_2);
	sys_init_run_level(INITCALL_LEVEL_PRE_KERNEL_3);

	osal_init();
	osal_start(bg_main_thread);

	while (1) {};
}
