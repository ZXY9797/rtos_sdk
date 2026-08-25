#include <init.h>

extern "C" int main(void);

extern const struct initcall_entry __init_EARLY_start[];
extern const struct initcall_entry __init_start[];
extern const struct initcall_entry __init_PRE_KERNEL_1_start[];
extern const struct initcall_entry __init_PRE_KERNEL_2_start[];
extern const struct initcall_entry __init_PRE_KERNEL_3_start[];
extern const struct initcall_entry __init_end[];

namespace {

const struct initcall_entry *run_initcalls(const struct initcall_entry *begin,
                                           const struct initcall_entry *end,
                                           int &error) {
    for (const struct initcall_entry *entry = begin; entry < end; ++entry) {
        error = entry->fn();
        if (error != 0) return entry;
    }
    return nullptr;
}

void rollback_initcalls(const struct initcall_entry *failed) {
    const struct initcall_entry *entry = failed + 1;
    while (entry > __init_start) {
        --entry;
        if (entry->rollback != nullptr) {
            (void)entry->rollback();
        }
    }
}

} // namespace

extern "C" void z_cstart(void) {
    int error = 0;
    volatile const struct initcall_entry *failed =
        run_initcalls(__init_EARLY_start, __init_PRE_KERNEL_1_start, error);
    if (failed == nullptr) failed = run_initcalls(
        __init_PRE_KERNEL_1_start, __init_PRE_KERNEL_2_start, error);
    if (failed == nullptr) failed = run_initcalls(
        __init_PRE_KERNEL_2_start, __init_PRE_KERNEL_3_start, error);
    if (failed == nullptr) failed = run_initcalls(
        __init_PRE_KERNEL_3_start, __init_end, error);
    if (failed != nullptr) {
        rollback_initcalls(
            const_cast<const struct initcall_entry *>(failed));
        (void)error;
        __asm volatile("bkpt #0");
        while (1) {}
    }

    (void)main();
    while (1) {}
}
