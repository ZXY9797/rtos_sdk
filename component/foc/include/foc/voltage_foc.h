#pragma once

#include <foc/svpwm.h>

namespace foc {

struct VoltageFocConfig {
    float nominal_bus_voltage_v {12.0F};
    // Fraction of the linear SVPWM voltage-vector radius.
    float maximum_modulation {0.85F};
};

class VoltageFoc {
public:
    [[nodiscard]] bool configure(const VoltageFocConfig &config);
    [[nodiscard]] PhaseDuty calculate(float electrical_angle_rad,
                                      float d_axis_voltage_v,
                                      float q_axis_voltage_v,
                                      float bus_voltage_v) const;

private:
    VoltageFocConfig config_ {};
    Svpwm svpwm_ {};
    bool is_configured_ {false};
};

[[nodiscard]] bool valid_voltage_foc_config(
    const VoltageFocConfig &config);

} // namespace foc
