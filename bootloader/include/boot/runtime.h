#pragma once

namespace boot {

/**
 * Service the product watchdog during bounded loader work.
 *
 * A product that enables CONFIG_BOOT_WATCHDOG must provide the implementation
 * in product/<name>/common/boot_watchdog.cc. The watchdog timeout must exceed
 * the worst-case duration of one flash-sector erase or program operation.
 */
void watchdog_service();

/**
 * Quiesce the active recovery transport before application handoff.
 * Custom transports must provide this function with their transport hooks.
 */
[[nodiscard]] bool transport_shutdown();

/**
 * Put the product watchdog into an application-safe handoff state.
 *
 * A watchdog-enabled product must either leave a sufficiently long window
 * for early application startup or transfer ownership without disabling a
 * watchdog that is locked on. Return false when a safe handoff is impossible.
 */
[[nodiscard]] bool watchdog_prepare_handoff();

} // namespace boot
