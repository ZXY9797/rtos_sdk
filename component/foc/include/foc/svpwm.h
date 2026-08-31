#pragma once

#include "types.h"

namespace foc {

struct PhaseDuty {
    float phase_u {0.5F};
    float phase_v {0.5F};
    float phase_w {0.5F};
};

class Svpwm {
public:
    Svpwm() = default;

    [[nodiscard]] bool generate_duty(const Vec2 &v_ab, float vbus,
                                     PhaseDuty &duty) const;

    // Generate three-phase SVPWM compare counts in [0, period].
    void generate(const Vec2 &v_ab, float vbus, uint32_t period,
                  uint32_t &duty_u, uint32_t &duty_v, uint32_t &duty_w);

    // Configure the usable linear modulation region.
    [[nodiscard]] bool set_modulation_limit(float limit);
    float modulation_limit() const { return modulation_max_; }

private:
    float modulation_max_ {0.95F};
};

} // namespace foc
