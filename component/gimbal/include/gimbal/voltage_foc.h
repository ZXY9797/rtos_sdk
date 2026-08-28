#pragma once

#include <gimbal/types.h>

#include <cstdint>

namespace gimbal {

struct VoltageFocConfig {
    float nominal_bus_voltage_v {12.0F};
    // Fraction of the linear SVPWM voltage-vector radius.
    float maximum_modulation {0.85F};
};

struct PhaseDuty {
    float phase_u {0.5F};
    float phase_v {0.5F};
    float phase_w {0.5F};
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
    bool is_configured_ {false};
};

[[nodiscard]] bool valid_voltage_foc_config(
    const VoltageFocConfig &config);

} // namespace gimbal
