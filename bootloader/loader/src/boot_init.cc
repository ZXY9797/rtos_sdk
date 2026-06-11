// bootloader z_cstart 覆盖
// 通过链接顺序抢占 embedded/system/init.cc 中的 z_cstart
// 不初始化 OSAL，仅做最小硬件初始化后进入 boot_main

namespace boot {
    void boot_main();
}

// 仅执行 initcall (EARLY → PRE_KERNEL)，不启动 OSAL
extern "C" void z_cstart(void);
extern "C" void z_cstart(void) {
    // 跳过 OSAL 初始化和 main()，直接进入 bootloader 逻辑
    boot::boot_main();
    while (1) {}
}
