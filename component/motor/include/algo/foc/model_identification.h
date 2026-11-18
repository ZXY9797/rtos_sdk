#pragma once
#include <cstdint>
namespace algo::motor
{
struct ElectricalFit
{
    float resistance_ohm{0}, inductance_h{0}, offset_v{0}, rms_v{0};
    uint32_t samples{0};
    bool valid{false};
};
// Locked-rotor trapezoidal RL regression. voltage is the voltage held during
// the interval ending at this current sample, not the next PWM request.
class ElectricalIdentifier
{
  public:
    void reset(float sample_time_s);
    bool sample(float current_a, float interval_voltage_v, float motor_velocity_rad_s);
    ElectricalFit result() const;

  private:
    void accumulate(unsigned index, float value);
    float sums_[10]{}, corrections_[10]{}, dt_{0}, previous_i_{0};
    uint32_t count_{0};
    bool primed_{false}, rejected_{false};
};
} // namespace algo::motor
