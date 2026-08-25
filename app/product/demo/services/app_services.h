#pragma once

#include <cstdint>

namespace app {

[[nodiscard]] bool confirm_boot_image();
[[nodiscard]] bool init_logging();
void print_device_registry();
void assert_required_devices();
int start_cli_poll();
void stop_cli_poll();
void print_periodic_diagnostics(uint32_t loop_count);

} // namespace app
