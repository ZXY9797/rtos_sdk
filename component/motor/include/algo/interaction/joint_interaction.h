#pragma once
#include <cstddef>
#include <cstdint>

namespace algo::interaction
{

struct SpringDamperOutput
{
    float position{0.0F}, damping{0.0F}, total{0.0F};
};
SpringDamperOutput spring_damper(float error, float velocity_error, float stiffness, float damping,
                                 float feedforward, float position_limit, float damping_limit,
                                 float total_limit);

struct TorqueMap
{
    // Signed, experimentally calibrated output-joint Nm per raw FOC Iq ampere.
    float joint_nm_per_iq_a{0.0F}, bias_nm{0.0F};
    bool valid() const;
    float torque(float iq_a) const { return joint_nm_per_iq_a * iq_a + bias_nm; }
    float current(float torque_nm) const { return (torque_nm - bias_nm) / joint_nm_per_iq_a; }
};

class TorqueCalibration
{
  public:
    void reset();
    bool add(float iq_a, float measured_joint_nm);
    bool finish(TorqueMap &output, float &rms_nm) const;
    uint32_t count() const { return count_; }

  private:
    uint32_t count_{0U};
    double sx_{0.0}, sy_{0.0}, sxx_{0.0}, sxy_{0.0}, syy_{0.0};
};

struct AdmittanceConfig
{
    float inertia{1.0F}, damping{10.0F}, stiffness{0.0F};
    float position_limit{0.5F}, velocity_limit{1.0F};
    float acceleration_limit{10.0F}, jerk_limit{100.0F}, force_deadband{0.02F};
};

struct MotionState
{
    float position{0.0F}, velocity{0.0F}, acceleration{0.0F};
};

class Admittance
{
  public:
    bool configure(const AdmittanceConfig &config, float sample_time_s);
    void reset() { state_ = {}; }
    bool update(float external_effort, float reference_effort, MotionState &output);
    void accept_position(float accepted_position);

  private:
    AdmittanceConfig config_{};
    MotionState state_{};
    float dt_{0.0F};
    bool configured_{false};
};

class EnergyTank
{
  public:
    bool configure(float initial_j, float maximum_j);
    float limit(float effort, float velocity, float dt);
    float preview(float effort, float velocity, float dt) const;
    float energy() const { return energy_; }

  private:
    float energy_{0.0F}, maximum_{0.0F};
};

struct CompensationConfig
{
    float gravity_bias_nm{0.0F}, gravity_amplitude_nm{0.0F}, gravity_phase_rad{0.0F};
    float viscous_nm_s{0.0F}, coulomb_nm{0.0F}, breakaway_nm{0.0F};
    float stribeck_velocity{0.1F}, sign_velocity{0.02F};
    float backlash_rad{0.0F}, backlash_rate_rad_s{0.1F};
    float load_inertia{0.0F};
};

struct ElasticConfig
{
    float stiffness_nm_rad{0.0F}, damping_nm_s{0.0F}, zero_rad{0.0F};
    float backlash_rad{0.0F}, observer_alpha{0.05F}, resonance_damping{0.0F};
};

struct ForceConfig
{
    float kp{0.1F}, ki{1.0F}, maximum_nm{1.0F}, rate_nm_s{10.0F};
    float contact_n{0.2F}, release_n{0.1F}, contact_time_s{0.02F};
    float lever_arm_m{0.0F}, approach_velocity{0.05F}, release_distance{0.05F};
    float maximum_travel_rad{0.5F}, approach_timeout_s{10.0F};
    float closing_direction{1.0F};
};

struct InteractionConfig
{
    TorqueMap torque{};
    AdmittanceConfig admittance{};
    CompensationConfig compensation{};
    ElasticConfig elastic{};
    ForceConfig force{};
    float servo_stiffness{0.0F}, servo_damping{0.0F};
    float stiffness_rate{100.0F}, damping_rate{100.0F};
    float torque_limit_nm{1.0F}, torque_rate_nm_s{10.0F};
    float initial_energy_j{0.5F}, maximum_energy_j{1.0F};
    float external_timeout_s{0.05F};
    float desired_inertia{0.0F};
    bool use_energy_tank{true}, use_model_external{false};
};

enum class JointMode : uint8_t
{
    Torque,
    Impedance,
    Admittance,
    Drag,
    Force,
    Grip,
    Elastic,
    InertiaShaping
};
enum class GripState : uint8_t
{
    Idle,
    Approach,
    Hold,
    Release,
    Finished,
    Fault
};

struct JointTarget
{
    float position_rad{0.0F}, velocity_rad_s{0.0F}, torque_nm{0.0F};
    float stiffness{0.0F}, damping{0.0F}, force_n{0.0F};
};

struct JointFeedback
{
    float position_rad{0.0F}, velocity_rad_s{0.0F}, load_velocity_rad_s{0.0F};
    float motor_equivalent_rad{0.0F}, motor_equivalent_velocity{0.0F}, iq_a{0.0F};
    float external_nm{0.0F}, effort_nm{0.0F}, force_n{0.0F};
    bool external_valid{false}, effort_valid{false}, force_valid{false};
};

struct JointResult
{
    float iq_a{0.0F}, torque_nm{0.0F}, reference_rad{0.0F}, reference_velocity{0.0F};
    float external_estimate_nm{0.0F}, elastic_effort_nm{0.0F}, energy_j{0.0F};
    float stiffness{0.0F}, damping{0.0F};
    GripState grip{};
    bool valid{false}, energize{false}, transition_complete{false}, contact{false};
};

// Fixed-state scalar interaction controller. External/model validity is explicit.
class JointInteraction
{
  public:
    bool configure(const InteractionConfig &config, float frequency_hz);
    bool start(JointMode mode, const JointTarget &target, const JointFeedback &feedback,
               float applied_iq_a = 0.0F);
    bool submit(const JointTarget &target);
    JointResult update(const JointFeedback &feedback);
    void track_applied(float iq_a);
    // Commit after all downstream limits, before sending the current request.
    float limit_applied(float iq_a, float velocity_rad_s);
    void stop();
    bool active() const { return active_; }
    JointMode mode() const { return mode_; }
    const JointResult &result() const { return result_; }
    const InteractionConfig &config() const { return config_; }
    bool set_cogging(const float *table_nm, size_t count);
    bool set_position_limits(float minimum_rad, float maximum_rad);

  private:
    float compensation(float q, float velocity) const;
    float force_control(float target_nm, float effort_nm, float velocity_rad_s);
    float impedance(float q, float velocity, float feedforward, const JointFeedback &feedback);
    InteractionConfig config_{};
    JointTarget target_{};
    JointResult result_{};
    Admittance admittance_{};
    EnergyTank tank_{};
    JointMode mode_{};
    GripState grip_{};
    float dt_{0.0F}, origin_{0.0F}, reference_{0.0F}, backlash_{0.0F};
    float force_integral_{0.0F}, force_unlimited_{0.0F}, applied_nm_{0.0F};
    float compensation_nm_{0.0F};
    float stiffness_{0.0F}, damping_{0.0F}, previous_velocity_{0.0F};
    float external_filtered_{0.0F}, elastic_filtered_{0.0F};
    float elapsed_{0.0F}, contact_elapsed_{0.0F};
    float cogging_[64]{};
    uint8_t cogging_count_{0U};
    bool configured_{false}, active_{false};
    float minimum_rad_{-3.4e38F}, maximum_rad_{3.4e38F};
};

} // namespace algo::interaction
