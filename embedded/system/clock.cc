#include <system/clock.h>
#include <osal.h>
#include <cmsis_core.h>

namespace system_clock {
uint32_t now_us()
{
    for (unsigned attempt = 0U; attempt < 3U; ++attempt) {
        const uint32_t tick = osal::Kernel::tick_count();
        const bool pending = (SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U;
        const uint32_t reload = SysTick->LOAD + 1U;
        const uint32_t remaining = SysTick->VAL;
        if (tick != osal::Kernel::tick_count()
            || pending != ((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U)) continue;
        if (reload < 2U || remaining >= reload) break;
        const uint64_t cycles = reload - remaining;
        return static_cast<uint32_t>((static_cast<uint64_t>(tick) + (pending ? 1U : 0U))
            * 1000000ULL / CONFIG_SYS_CLOCK_TICKS_PER_SEC
            + cycles * 1000000ULL / CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC);
    }
    return osal::Kernel::uptime_ms() * 1000U;
}
}
