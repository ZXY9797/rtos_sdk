#pragma once
#include "control/brake_control.h"
#include <algo/interaction.h>
#include <algo/robotics.h>
#include <algo/motor/execution_limits.h>

namespace app
{
struct ElectricalModel
{
    float resistance{0}, ld{0}, lq{0}, flux{0};
    bool decoupling{false};
};
struct CartesianModel
{
    float length_m{0}, mass_kg{0}, com_m{0}, inertia_kg_m2{0};
    algo::robotics::CartesianController<1>::Config controller{};
};
struct InteractionConfig
{
    algo::motor::ExecutionConfig execution{};
    algo::interaction::InteractionConfig joint{};
    BrakeConfig brake{};
    ElectricalModel electrical{};
    CartesianModel cartesian{};
    float cogging_nm[64]{};
};
inline constexpr unsigned kInteractionSections = 11;
// One schema drives wire encoding, decoding and persistence. Booleans are encoded
// as exact float32 0/1; no ABI-dependent C++ object representation is stored.
template <class Visitor> bool visit_config(InteractionConfig &c, unsigned section, Visitor &&f)
{
    auto &e = c.execution;
    auto &j = c.joint;
    switch (section)
    {
    case 0:
        f(e.phase_current_a);
        f(e.continuous_current_a);
        f(e.excess_i2t_a2_s);
        f(e.maximum_velocity_rad_s);
        f(e.current_slew_a_s);
        f(e.command_timeout_s);
        f(e.temperature_timeout_s);
        f(e.minimum_position_rad);
        f(e.maximum_position_rad);
        f(e.hard_limit_margin_rad);
        f(e.following_error_rad);
        f(e.following_time_s);
        f(e.motoring_power_w);
        f(e.regenerative_power_w);
        f(e.derate_start_c);
        f(e.derate_stop_c);
        f(e.position_limits);
        f(e.mechanical_power);
        break;
    case 1:
        f(j.torque.joint_nm_per_iq_a);
        f(j.torque.bias_nm);
        f(j.servo_stiffness);
        f(j.servo_damping);
        f(j.stiffness_rate);
        f(j.damping_rate);
        f(j.torque_limit_nm);
        f(j.torque_rate_nm_s);
        f(j.initial_energy_j);
        f(j.maximum_energy_j);
        f(j.external_timeout_s);
        f(j.use_energy_tank);
        f(j.use_model_external);
        f(j.desired_inertia);
        break;
    case 2:
    {
        auto &a = j.admittance;
        f(a.inertia);
        f(a.damping);
        f(a.stiffness);
        f(a.position_limit);
        f(a.velocity_limit);
        f(a.acceleration_limit);
        f(a.jerk_limit);
        f(a.force_deadband);
        break;
    }
    case 3:
    {
        auto &a = j.compensation;
        f(a.gravity_bias_nm);
        f(a.gravity_amplitude_nm);
        f(a.gravity_phase_rad);
        f(a.viscous_nm_s);
        f(a.coulomb_nm);
        f(a.breakaway_nm);
        f(a.stribeck_velocity);
        f(a.sign_velocity);
        f(a.backlash_rad);
        f(a.backlash_rate_rad_s);
        f(a.load_inertia);
        break;
    }
    case 4:
    {
        auto &a = j.elastic;
        f(a.stiffness_nm_rad);
        f(a.damping_nm_s);
        f(a.zero_rad);
        f(a.backlash_rad);
        f(a.observer_alpha);
        f(a.resonance_damping);
        break;
    }
    case 5:
    {
        auto &a = j.force;
        f(a.kp);
        f(a.ki);
        f(a.maximum_nm);
        f(a.rate_nm_s);
        f(a.contact_n);
        f(a.release_n);
        f(a.contact_time_s);
        f(a.lever_arm_m);
        f(a.approach_velocity);
        f(a.release_distance);
        f(a.maximum_travel_rad);
        f(a.approach_timeout_s);
        f(a.closing_direction);
        break;
    }
    case 6:
    {
        auto &a = c.electrical;
        f(a.resistance);
        f(a.ld);
        f(a.lq);
        f(a.flux);
        f(a.decoupling);
        break;
    }
    case 7:
    {
        auto &a = c.cartesian;
        f(a.length_m);
        f(a.mass_kg);
        f(a.com_m);
        f(a.inertia_kg_m2);
        for (auto *v : {&a.controller.stiffness, &a.controller.damping, &a.controller.force_kp,
                        &a.controller.force_ki, &a.controller.wrench_limit})
            for (auto &x : *v)
                f(x);
        for (auto &x : a.controller.force_axes)
            f(x);
        f(a.controller.dynamics_feedforward);
        break;
    }
    case 8: // Task-space admittance uses SI translational / rotational parameters.
        for (auto &a : c.cartesian.controller.admittance)
        {
            f(a.inertia);
            f(a.damping);
            f(a.stiffness);
            f(a.position_limit);
            f(a.velocity_limit);
            f(a.acceleration_limit);
            f(a.jerk_limit);
            f(a.force_deadband);
        }
        break;
    case 9:
        for (float &x : c.cogging_nm)
            f(x);
        break;
    case 10: {
        auto& b = c.brake;
        f(b.hold_a); f(b.tolerance_a); f(b.preload_ms); f(b.release_ms); f(b.engage_ms);
        f(b.travel_rad); f(b.still_rad_s); f(b.deceleration); f(b.kp_a_rad); f(b.kd_a_s_rad);
        f(b.collision_nm); f(b.collision_ms);
        break;
    }
    default:
        return false;
    }
    return true;
}
} // namespace app
