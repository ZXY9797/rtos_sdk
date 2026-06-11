#include <boot/image.h>
#include <boot/product_info.h>
#include <boot/flash_map.h>
#include <boot/boot_ctrl.h>

namespace boot {

// 由 boot_logic.cc 实现
void boot_logic();

void boot_main() {
    boot_logic();
    while (1) {}
}

} // namespace boot
