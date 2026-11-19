#pragma once

#include <drivers/pwm.h>
#include <dt-bindings/pwm/gd32-timer.h>
#include <gd32f50x_trigsel.h>

namespace hal {

namespace gd32_timer {
[[nodiscard]] Status configure_adc_compare(PwmBase& timer, uint32_t count);
[[nodiscard]] Status configure_adc_update(PwmBase& timer);
}

template <uintptr_t Base, unsigned Event>
struct Gd32TimerTrigger {
    static_assert(Base == TIMER0_BASE || Base == TIMER7_BASE, "Unsupported ADC trigger timer");
    static_assert(Event == GD32_TIMER_COMPARE_CH3 || Event == GD32_TIMER_UPDATE,
                  "Unsupported ADC trigger event");
    static constexpr uint8_t input = Base == TIMER0_BASE
        ? (Event == GD32_TIMER_COMPARE_CH3 ? TRIGSEL_INPUT_TIMER0_CH3 : TRIGSEL_INPUT_TIMER0_TRGO)
        : (Event == GD32_TIMER_COMPARE_CH3 ? TRIGSEL_INPUT_TIMER7_CH3 : TRIGSEL_INPUT_TIMER7_TRGO);

    template <PwmChannel Channel>
    static Status configure(Pwm<Base, Channel>& timer, uint32_t compare_count) {
        static_assert(Event == GD32_TIMER_COMPARE_CH3, "Sample offset requires a compare event");
        return gd32_timer::configure_adc_compare(timer, compare_count);
    }

    template <PwmChannel Channel>
    static Status configure(Pwm<Base, Channel>& timer) {
        static_assert(Event == GD32_TIMER_UPDATE, "Periodic sampling requires an update event");
        return gd32_timer::configure_adc_update(timer);
    }
};

} // namespace hal
