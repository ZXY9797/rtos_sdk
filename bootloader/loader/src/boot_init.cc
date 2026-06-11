// bootloader z_cstart 覆盖
// 通过链接顺序抢占 embedded/system/init.cc 中的 z_cstart。
// loader 只执行 EARLY/PRE_KERNEL initcall，不启动 OSAL 调度器。

#include <init.h>

namespace boot {
    void boot_main();
}

extern const struct initcall_entry __init_EARLY_start[];
extern const struct initcall_entry __init_PRE_KERNEL_1_start[];
extern const struct initcall_entry __init_PRE_KERNEL_2_start[];
extern const struct initcall_entry __init_PRE_KERNEL_3_start[];
extern const struct initcall_entry __init_end[];

static void boot_run_initcalls(const struct initcall_entry *begin,
                               const struct initcall_entry *end)
{
    for (const struct initcall_entry *entry = begin; entry < end; ++entry) {
        entry->fn();
    }
}

extern "C" void z_cstart(void);
extern "C" void z_cstart(void) {
    boot_run_initcalls(__init_EARLY_start, __init_PRE_KERNEL_1_start);
    boot_run_initcalls(__init_PRE_KERNEL_1_start, __init_PRE_KERNEL_2_start);
    boot_run_initcalls(__init_PRE_KERNEL_2_start, __init_PRE_KERNEL_3_start);
    boot_run_initcalls(__init_PRE_KERNEL_3_start, __init_end);

    boot::boot_main();
    while (1) {}
}
