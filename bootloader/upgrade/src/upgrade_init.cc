#include <init.h>

extern "C" int main(void);

extern const struct initcall_entry __init_EARLY_start[];
extern const struct initcall_entry __init_PRE_KERNEL_1_start[];
extern const struct initcall_entry __init_PRE_KERNEL_2_start[];
extern const struct initcall_entry __init_PRE_KERNEL_3_start[];
extern const struct initcall_entry __init_end[];

namespace {

void run_initcalls(const struct initcall_entry *begin,
                   const struct initcall_entry *end) {
    for (const struct initcall_entry *entry = begin; entry < end; ++entry) {
        entry->fn();
    }
}

} // namespace

extern "C" void z_cstart(void) {
    run_initcalls(__init_EARLY_start, __init_PRE_KERNEL_1_start);
    run_initcalls(__init_PRE_KERNEL_1_start, __init_PRE_KERNEL_2_start);
    run_initcalls(__init_PRE_KERNEL_2_start, __init_PRE_KERNEL_3_start);
    run_initcalls(__init_PRE_KERNEL_3_start, __init_end);

    (void)main();
    while (1) {}
}
