#pragma once

#include <gimbal/safety_manager.h>

#include <cstdint>

namespace app {

[[nodiscard]] int start_gimbal();
[[nodiscard]] bool wait_gimbal_boot_ready(uint32_t timeout_ms);
void stop_gimbal();
[[nodiscard]] bool request_gimbal_arm();
void request_gimbal_disarm();
[[nodiscard]] gimbal::SafetyOutput gimbal_safety_status();
void heartbeat_gimbal_main();
void print_gimbal_diagnostics();

} // namespace app
