#pragma once

// Retain typed references to both host callbacks without registering a
// firmware linker section in the MSVC compile-only contract.
#define SYS_INIT_ROLLBACK(init, deinit, level, priority) \
    [[maybe_unused]] static int (*const host_initcall_callbacks[])() = {init, deinit}
