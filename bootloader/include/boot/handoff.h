#pragma once

#include <cstdint>

namespace boot {

/**
 * Transfer control to a validated Cortex-M vector table.
 *
 * This function disables loader-owned interrupt sources, restores the
 * architectural interrupt masks expected after reset, switches MSP, and
 * branches without executing a C/C++ function epilogue on the new stack.
 */
[[noreturn]] void handoff_to_image(uint32_t vector_addr,
                                   uint32_t stack_pointer,
                                   uint32_t reset_handler);

} // namespace boot
