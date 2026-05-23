#include "init.h"
#include "device.h"
#include <soc.h>
#include <zephyr/kernel.h>

extern int do_device_init(const struct device *dev);

extern const struct init_entry __init_start[];
extern const struct init_entry __init_EARLY_start[];
extern const struct init_entry __init_PRE_KERNEL_1_start[];
extern const struct init_entry __init_PRE_KERNEL_2_start[];
extern const struct init_entry __init_POST_KERNEL_start[];
extern const struct init_entry __init_APPLICATION_start[];
extern const struct init_entry __init_end[];

enum init_level {
	INIT_LEVEL_EARLY = 0,
	INIT_LEVEL_PRE_KERNEL_1,
	INIT_LEVEL_PRE_KERNEL_2,
	INIT_LEVEL_POST_KERNEL,
	INIT_LEVEL_APPLICATION,
#ifdef CONFIG_SMP
	INIT_LEVEL_SMP,
#endif /* CONFIG_SMP */
};

#ifdef CONFIG_SMP
extern const struct init_entry __init_SMP_start[];
#endif /* CONFIG_SMP */

static void sys_init_run_level(enum init_level level)
{
	static const struct init_entry *levels[] = {
		__init_EARLY_start,
		__init_PRE_KERNEL_1_start,
		__init_PRE_KERNEL_2_start,
		__init_POST_KERNEL_start,
		__init_APPLICATION_start,
#ifdef CONFIG_SMP
		__init_SMP_start,
#endif /* CONFIG_SMP */
		/* End marker */
		__init_end,
	};
	const struct init_entry *entry;

	for (entry = levels[level]; entry < levels[level+1]; entry++) {
		const struct device *dev = entry->dev;
		int result = 0;

		if (dev != NULL) {
			if ((dev->flags & DEVICE_FLAG_INIT_DEFERRED) == 0U) {
				result = do_device_init(dev);
			}
		} else {
			result = entry->init_fn();
		}
        ARG_UNUSED(result);
	}
}

#define CONFIG_STATIC_INIT_GNU
#ifdef CONFIG_STATIC_INIT_GNU

extern void (*__init_array_start[])();
extern void (*__init_array_end[])();

static void z_static_init_gnu(void)
{
	void	(**fn)();

	for (fn = __init_array_start; fn != __init_array_end; fn++) {
		/* MWDT toolchain sticks a NULL at the end of the array */
		if (*fn == NULL) {
			break;
		}
		(**fn)();
	}
}

#endif

/* Background main thread for kernel initialization */
static struct k_thread bg_thread;
static K_THREAD_STACK_DEFINE(bg_thread_stack, 2048);

void bg_main_thread(void *arg, void *p2, void *p3)
{
    ARG_UNUSED(arg);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    sys_init_run_level(INIT_LEVEL_POST_KERNEL);
    soc_late_init_hook();

#ifdef CONFIG_STATIC_INIT_GNU
	z_static_init_gnu();
#endif /* CONFIG_STATIC_INIT_GNU */
	/* Final init level before app starts */
	sys_init_run_level(INIT_LEVEL_APPLICATION);

    extern int main();
    main();
}

void z_cstart(void)
{
    sys_init_run_level(INIT_LEVEL_EARLY);

    soc_early_init_hook();
    sys_init_run_level(INIT_LEVEL_PRE_KERNEL_1);
    sys_init_run_level(INIT_LEVEL_PRE_KERNEL_2);

    /* Kernel is now initialized, start the background thread */
    /* The K_THREAD_DEFINE macro automatically starts the thread */

    /* Should never reach here */
    while(1){};
}
