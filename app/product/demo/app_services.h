#pragma once

#include <cstdint>

namespace demo_app {

void confirm_boot_image();
void init_logging();
void print_device_registry();
void assert_required_devices();
void start_cli_poll();
void print_periodic_diagnostics(uint32_t loop_count);

} // 命名空间 demo_app
