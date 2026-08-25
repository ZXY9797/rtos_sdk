// Loader-specific startup. It initializes the bare-metal OSAL but never
// starts an RTOS scheduler.

#include <init.h>
#include <osal.h>

namespace boot {
void boot_main();
}

extern const struct initcall_entry __init_start[];
extern const struct initcall_entry __init_EARLY_start[];
extern const struct initcall_entry __init_PRE_KERNEL_1_start[];
extern const struct initcall_entry __init_PRE_KERNEL_2_start[];
extern const struct initcall_entry __init_PRE_KERNEL_3_start[];
extern const struct initcall_entry __init_POST_KERNEL_start[];
extern const struct initcall_entry __init_APPLICATION_start[];
extern const struct initcall_entry __init_end[];

using Constructor = void (*)(void);
extern Constructor __preinit_array_start[];
extern Constructor __preinit_array_end[];
extern Constructor __init_array_start[];
extern Constructor __init_array_end[];

extern "C" {
volatile const struct initcall_entry *g_boot_init_failed_entry;
volatile int g_boot_init_failed_error;
volatile const struct initcall_entry *g_boot_rollback_failed_entry;
volatile int g_boot_rollback_failed_error;
}

namespace {

void run_constructors(Constructor *begin, Constructor *end) {
    for (Constructor *constructor = begin; constructor < end;
         ++constructor) {
        (*constructor)();
    }
}

const struct initcall_entry *run_initcalls(
    const struct initcall_entry *begin,
    const struct initcall_entry *end,
    int &error) {
    for (const struct initcall_entry *entry = begin; entry < end; ++entry) {
        error = entry->fn();
        if (error != 0) {
            return entry;
        }
    }
    return nullptr;
}

void rollback_initcalls(const struct initcall_entry *failed) {
    const struct initcall_entry *entry = failed + 1;
    while (entry > __init_start) {
        --entry;
        if (entry->rollback == nullptr) {
            continue;
        }
        const int error = entry->rollback();
        if (error != 0 && g_boot_rollback_failed_entry == nullptr) {
            g_boot_rollback_failed_entry = entry;
            g_boot_rollback_failed_error = error;
        }
    }
}

void rollback_before(const struct initcall_entry *end) {
    const struct initcall_entry *entry = end;
    while (entry > __init_start) {
        --entry;
        if (entry->rollback == nullptr) {
            continue;
        }
        const int error = entry->rollback();
        if (error != 0 && g_boot_rollback_failed_entry == nullptr) {
            g_boot_rollback_failed_entry = entry;
            g_boot_rollback_failed_error = error;
        }
    }
}

[[noreturn]] void halt(void) {
    while (true) {
        __asm volatile("wfi");
    }
}

[[noreturn]] void halt_init_failure(
    const struct initcall_entry *failed,
    int error) {
    g_boot_init_failed_entry = failed;
    g_boot_init_failed_error = error;
    rollback_initcalls(failed);
    halt();
}

[[noreturn]] void halt_osal_failure(int error) {
    g_boot_init_failed_entry = nullptr;
    g_boot_init_failed_error = error;
    rollback_before(__init_POST_KERNEL_start);
    halt();
}

const struct initcall_entry *run_pre_kernel(int &error) {
    const struct initcall_entry *failed = run_initcalls(
        __init_EARLY_start, __init_PRE_KERNEL_1_start, error);
    if (failed != nullptr) {
        return failed;
    }
    failed = run_initcalls(__init_PRE_KERNEL_1_start,
                           __init_PRE_KERNEL_2_start, error);
    if (failed != nullptr) {
        return failed;
    }
    failed = run_initcalls(__init_PRE_KERNEL_2_start,
                           __init_PRE_KERNEL_3_start, error);
    if (failed != nullptr) {
        return failed;
    }
    return run_initcalls(__init_PRE_KERNEL_3_start,
                         __init_POST_KERNEL_start, error);
}

const struct initcall_entry *run_post_kernel(int &error) {
    const struct initcall_entry *failed = run_initcalls(
        __init_POST_KERNEL_start, __init_APPLICATION_start, error);
    if (failed != nullptr) {
        return failed;
    }
    return run_initcalls(__init_APPLICATION_start, __init_end, error);
}

} // namespace

extern "C" void z_cstart(void);
extern "C" void z_cstart(void) {
    run_constructors(__preinit_array_start, __preinit_array_end);
    run_constructors(__init_array_start, __init_array_end);

    int error = 0;
    const struct initcall_entry *failed = run_pre_kernel(error);
    if (failed != nullptr) {
        halt_init_failure(failed, error);
    }

    error = osal_init();
    if (error != 0) {
        halt_osal_failure(error);
    }

    failed = run_post_kernel(error);
    if (failed != nullptr) {
        halt_init_failure(failed, error);
    }

    boot::boot_main();
    halt();
}
