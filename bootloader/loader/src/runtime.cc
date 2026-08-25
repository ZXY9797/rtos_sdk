#include <boot/runtime.h>

extern "C" {
void *__dso_handle = nullptr;
}

// The loader never performs a C/C++ process exit. Suppress global-destructor
// registration so static device objects do not pull the hosted exit runtime.
extern "C" int __aeabi_atexit(
    void *object,
    void (*destructor)(void *),
    void *dso_handle) {
    (void)object;
    (void)destructor;
    (void)dso_handle;
    return 0;
}

namespace boot {

#if !defined(CONFIG_BOOT_WATCHDOG)
__attribute__((weak))
void watchdog_service() {
}

__attribute__((weak))
bool watchdog_prepare_handoff() {
    return true;
}
#endif

} // namespace boot
