#include <init.h>
#include <osal.h>
#include <arch/arm/cortex_m/fault.h>

extern "C" int main(void);

using CppInitFunction = void (*)();

// Linker-defined ABI symbols must have global C linkage. Declaring them
// inside an unnamed namespace gives them internal linkage with newer GCC and
// leaves references that the linker script cannot satisfy.
extern "C" {
extern CppInitFunction __preinit_array_start[];
extern CppInitFunction __preinit_array_end[];
extern CppInitFunction __init_array_start[];
extern CppInitFunction __init_array_end[];
}

namespace {

void run_cpp_init_array(CppInitFunction *begin, CppInitFunction *end)
{
	for (CppInitFunction *entry = begin; entry < end; ++entry) {
		if (*entry != nullptr) {
			(*entry)();
		}
	}
}

void initialize_cpp_runtime()
{
	static bool initialized;
	if (initialized) {
		return;
	}

	run_cpp_init_array(__preinit_array_start, __preinit_array_end);
	run_cpp_init_array(__init_array_start, __init_array_end);
	initialized = true;
}

} // namespace

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

[[noreturn]] static void halt_on_init_error(const struct initcall_entry *entry,
                                            int error)
{
	g_init_failed_entry = entry;
	g_init_failed_error = error;
	rollback_initialized(entry);
	hal::fault::panic(hal::fault::FatalReason::InitFailure, error,
	                  entry->name, 0U);
}

[[noreturn]] static void halt_on_osal_init_error(int error)
{
	g_init_failed_entry = nullptr;
	g_init_failed_error = error;
	hal::fault::panic(hal::fault::FatalReason::OsalFailure, error,
	                  "osal_init", 0U);
}

[[noreturn]] static void halt_on_osal_start_error(int error)
{
	g_init_failed_entry = nullptr;
	g_init_failed_error = error;
	rollback_before(__init_POST_KERNEL_start);
	hal::fault::panic(hal::fault::FatalReason::OsalFailure, error,
	                  "osal_start", 0U);
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

	const int result = main();
	hal::fault::panic(hal::fault::FatalReason::MainReturned, result,
	                  "main", 0U);
}

extern "C" void z_cstart(void)
{
	/*
	 * RTOS-backed global objects may create kernel primitives in their
	 * constructors. Initialise the kernel substrate before running C++
	 * constructors, but do not start scheduling until all pre-kernel
	 * initcalls have completed successfully.
	 */
	const int osal_init_error = osal_init();
	if (osal_init_error != 0) {
		halt_on_osal_init_error(osal_init_error);
	}
	initialize_cpp_runtime();

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

	const int osal_start_error = osal_start(bg_main_thread);
	/* A successful scheduler start never returns. */
	halt_on_osal_start_error(
		osal_start_error != 0 ? osal_start_error : -1);

}
