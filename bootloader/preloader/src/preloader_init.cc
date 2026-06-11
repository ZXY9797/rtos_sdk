// Preloader z_cstart 覆盖
// 最精简启动，不初始化 OSAL，不调用 main()

namespace preloader {
    void main();
}

extern "C" void z_cstart(void);
extern "C" void z_cstart(void) {
    preloader::main();
    while (1) {}
}
