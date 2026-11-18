#pragma once
#include <cstdint>

namespace algo::motor
{

enum class ExecutionFault : uint32_t
{
    PhaseCurrent = 1U,
    Overspeed = 2U,
    Position = 4U,
    Following = 8U,
    CommandStale = 16U,
    TemperatureStale = 32U,
    I2t = 64U,
    ExternalSignal = 128U,
    PowerBudget = 256U,
    InteractionController = 512U,
};

struct ExecutionConfig
{
    float phase_current_a{6.0F};
    float continuous_current_a{2.2F};
    float excess_i2t_a2_s{4.0F};
    float maximum_velocity_rad_s{40.0F};
    float current_slew_a_s{1000.0F};
    float command_timeout_s{0.5F};
    float temperature_timeout_s{0.1F};
    float minimum_position_rad{0.0F}, maximum_position_rad{0.0F};
    float hard_limit_margin_rad{0.1F};
    float following_error_rad{0.0F}, following_time_s{0.1F};
    float motoring_power_w{0.0F}, regenerative_power_w{0.0F};
    float derate_start_c{90.0F}, derate_stop_c{120.0F};
    bool position_limits{false};
    bool mechanical_power{false};
};

struct ExecutionState
{
    uint32_t faults{0U};
    float command_age_s{0.0F}, invalid_temperature_s{0.0F};
    float excess_i2t_a2_s{0.0F}, electrical_power_w{0.0F};
    float applied_current_a{0.0F}, current_limit_a{0.0F};
    bool limited{false};
};

class ExecutionLimits
{
  public:
    bool configure(const ExecutionConfig &config, float current_limit_a, float current_frequency_hz,
                   float joint_frequency_hz,bool preserve_state=false);
    void reset();
    void touch_command() { state_.command_age_s = 0.0F; }
    void current_sample(float u, float v, float w, float iq, float power_w, bool active);
    void supervise(float position, float velocity, float following_error, bool follow, bool active,
                   bool normal_control, float temperature_c, bool temperature_valid);
    float apply(float requested_current, float position, float velocity, float joint_nm_per_iq_a,
                bool active,float bias_nm=0.0F);
    void trip(ExecutionFault fault) { state_.faults |= static_cast<uint32_t>(fault); }
    bool clear();
    void track_applied(float current_a) { state_.limited |= current_a != state_.applied_current_a;state_.applied_current_a=current_a; }
    const ExecutionState &state() const { return state_; }
    const ExecutionConfig &config() const { return config_; }

  private:
    ExecutionConfig config_{};
    ExecutionState state_{};
    float current_limit_{0.0F}, fast_dt_{0.0F}, joint_dt_{0.0F};
    float following_s_{0.0F}, maximum_phase_a_{0.0F}, measured_iq_a_{0.0F};
    float last_position_{0.0F}, last_velocity_{0.0F}, derating_{1.0F};
    bool configured_{false};
};

} // namespace algo::motor
