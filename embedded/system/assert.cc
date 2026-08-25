#include <assert.h>
#include <arch/arm/cortex_m/fault.h>

extern "C" void hal_assert_handler(const char *expr, const char *func, int line)
{
    (void)expr;
    hal::fault::panic(hal::fault::FatalReason::Assert, line, func,
                      static_cast<uint32_t>(line));
}
