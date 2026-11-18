#include <algo/interaction.h>
#include <algo/robotics.h>
#include <algo/motor/execution_limits.h>
#include <algo/foc/current_loop.h>
#include <algo/foc/model_identification.h>
#include <cassert>
#include <cmath>
#include <limits>
using namespace algo::interaction;
static bool near(float a, float b, float tolerance = 1e-4F) { return std::fabs(a - b) < tolerance; }

static void test_admittance()
{
    Admittance a;
    AdmittanceConfig c;
    c.inertia = 0.2F;
    c.damping = 2;
    c.stiffness = 10;
    c.position_limit = 0.2F;
    c.force_deadband = 0;
    c.jerk_limit = 1000;
    assert(a.configure(c, 0.001F));
    MotionState s;
    for (unsigned i = 0; i < 30000; ++i)
    {
        assert(a.update(1, 0, s));
        assert(std::fabs(s.position) <= c.position_limit);
    }
    assert(near(s.position, 0.1F, 0.001F));
    for (unsigned i = 0; i < 10000; ++i)
        assert(a.update(100, 0, s));
    assert(near(s.position, 0.2F));
    assert(s.velocity == 0);
    for (unsigned i = 0; i < 1000; ++i)
        assert(a.update(-10, 0, s));
    assert(s.position < 0);
    assert(!a.update(std::numeric_limits<float>::quiet_NaN(), 0, s));
    c.damping = -1;
    assert(!a.configure(c, 0.001F));
    EnergyTank tank;
    assert(tank.configure(0.5F, 1));
    float work = 0;
    for (unsigned i = 0; i < 1000; ++i)
        work += tank.limit(1, 1, 0.001F) * 0.001F;
    assert(work <= 0.5001F);
    assert(tank.energy() >= 0);
    assert(tank.limit(-1, 1, 0.001F) == -1);
    assert(tank.energy() == 0);
}
static void test_torque_and_interaction()
{
    TorqueCalibration fit;
    for (int i = -10; i <= 10; ++i)
        assert(fit.add(i * 0.1F, -2 * i * 0.1F + 0.1F));
    TorqueMap map;
    float rms;
    assert(fit.finish(map, rms));
    assert(near(map.joint_nm_per_iq_a, -2));
    assert(near(map.bias_nm, 0.1F));
    assert(rms < 0.001F);
    TorqueCalibration constant;
    for (int i = 0; i < 10; ++i)
        constant.add(1, 2);
    assert(!constant.finish(map, rms));
    JointInteraction ctrl;
    InteractionConfig c;
    c.torque = {2, 0};
    c.use_energy_tank = false;
    c.servo_stiffness = 10;
    c.servo_damping = 1;
    c.stiffness_rate = 10000;
    c.damping_rate = 10000;
    c.torque_limit_nm = 2;
    c.torque_rate_nm_s = 1000;
    c.force.lever_arm_m = 0.02F;
    assert(ctrl.configure(c, 1000));
    JointFeedback f;
    JointTarget t;
    t.position_rad = 0.1F;
    t.stiffness = 10;
    t.damping = 1;
    assert(ctrl.start(JointMode::Impedance, t, f));
    JointResult r;
    for (int i = 0; i < 30; ++i)
    {
        r = ctrl.update(f);
        assert(r.valid);
        ctrl.track_applied(r.iq_a);
    }
    assert(near(r.torque_nm, 1));
    assert(near(r.iq_a, 0.5F));
    assert(r.transition_complete);
    assert(!ctrl.start(JointMode::Drag, t, f));
    f.external_valid = true;
    f.external_nm = 0.2F;
    assert(ctrl.start(JointMode::Drag, t, f));
    for (int i = 0; i < 2000; ++i)
    {
        r = ctrl.update(f);
        ctrl.track_applied(r.iq_a);
    }
    assert(r.valid && r.reference_rad > 0);
    f.external_valid = false;
    assert(!ctrl.update(f).energize);
    f.force_valid = true;
    f.force_n = 0;
    t.force_n = 1;
    assert(ctrl.start(JointMode::Force, t, f));
    for (int i = 0; i < 10000; ++i)
    {
        r = ctrl.update(f);
        ctrl.track_applied(0);
    }
    f.force_n = 1;
    r = ctrl.update(f);
    assert(std::fabs(r.torque_nm) < 0.1F); // no hidden PI windup after actuator clipping
    ctrl.stop();
    c.force.contact_time_s = 0.003F;
    assert(ctrl.configure(c, 1000));
    t.force_n = 1;
    f.force_n = 0;
    assert(ctrl.start(JointMode::Grip, t, f));
    r = ctrl.update(f);
    assert(r.grip == GripState::Approach);
    f.force_n = 0.3F;
    for (int i = 0; i < 4; ++i)
    {
        r = ctrl.update(f);
        ctrl.track_applied(r.iq_a);
    }
    assert(r.grip == GripState::Hold);
    t.force_n = 0;
    assert(ctrl.submit(t));
    f.force_n = 0;
    for (int i = 0; i < 2000; ++i)
    {
        r = ctrl.update(f);
        f.position_rad = r.reference_rad;
        ctrl.track_applied(r.iq_a);
        if (!r.energize)
            break;
    }
    assert(r.grip == GripState::Finished && !r.energize);
    ctrl.stop();
    c.force.approach_timeout_s = 0.01F;
    assert(ctrl.configure(c, 1000));
    t.force_n = 1;
    assert(ctrl.start(JointMode::Grip, t, f));
    for (int i = 0; i < 20; ++i)
    {
        r = ctrl.update(f);
        if (!r.valid)
            break;
    }
    assert(!r.valid && !r.energize);
    ctrl.stop();
    c.elastic.stiffness_nm_rad = 100;
    c.use_model_external = true;
    assert(ctrl.configure(c, 1000));
    f = {};
    f.motor_equivalent_rad = 0.01F;
    t = {};
    assert(ctrl.start(JointMode::Elastic, t, f));
    for (int i = 0; i < 200; ++i)
        r = ctrl.update(f);
    assert(near(r.elastic_effort_nm, 1, 0.001F));
    assert(r.external_estimate_nm < -.99F);
}
static void test_limits()
{
    using namespace algo::motor;
    ExecutionLimits l;
    ExecutionConfig c;
    c.command_timeout_s = 0.01F;
    assert(l.configure(c, 2.2F, 20000, 4000));
    l.touch_command();
    for (int i = 0; i < 20; ++i)
        l.supervise(0, 0, 0, false, true, true, 25, true);
    assert(l.state().faults == 0);
    float out = l.apply(10, 0, 0, 1, true);
    assert(out <= 0.2501F);
    for (int i = 0; i < 30; ++i)
        l.supervise(0, 0, 0, false, true, true, 25, true);
    assert(l.state().faults & uint32_t(ExecutionFault::CommandStale));
    assert(l.apply(1, 0, 0, 1, true) == 0);
    assert(l.clear());
    l.current_sample(7, 0, 0, 0, 0, true);
    assert(l.state().faults & 1);
    assert(!l.clear());
    l.current_sample(0, 0, 0, 0, 0, false);
    assert(l.clear());
    c.command_timeout_s = 0;
    c.temperature_timeout_s = 0.001F;
    assert(l.configure(c, 2.2F, 20000, 4000));
    for (int i = 0; i < 10; ++i)
        l.supervise(0, 0, 0, false, true, true, 25, false);
    assert(l.state().faults & uint32_t(ExecutionFault::TemperatureStale));
    c.position_limits = true;
    c.minimum_position_rad = -1;
    c.maximum_position_rad = 1;
    c.mechanical_power = true;
    c.motoring_power_w = 0.2F;
    assert(l.configure(c, 2.2F, 20000, 4000));
    for (int i = 0; i < 100; ++i)
        out = l.apply(2, 0, 1, 2, true);
    assert(out <= 0.10001F);
    assert(l.apply(2, 1, 0, 2, true) == 0);
}
static void test_robotics()
{
    using namespace algo::robotics;
    SerialChain<2> chain;
    Link a, b;
    a.joint_to_link.position = {1, 0, 0};
    a.center_of_mass = {-.5F, 0, 0};
    a.mass = 1;
    a.inertia[2][2] = 1.0F / 12;
    b = a;
    assert(chain.configure({a, b}));
    SerialChain<2>::State s;
    SerialChain<2>::Joints q{.3F, -.6F}, v{.2F, -.1F}, zero{}, t{};
    assert(chain.forward(q, s));
    assert(near(s.tool.position[0], std::cos(q[0]) + std::cos(q[0] + q[1])));
    const auto j = chain.jacobian(s);
    for (unsigned i = 0; i < 2; ++i)
    {
        auto dq = q;
        dq[i] += 0.0005F;
        SerialChain<2>::State shifted;
        assert(chain.forward(dq, shifted));
        for (unsigned k = 0; k < 3; ++k)
            assert(near((shifted.tool.position[k] - s.tool.position[k]) / .0005F, j[k][i], .002F));
    }
    SerialChain<2>::MassMatrix m;
    assert(chain.mass_matrix(q, m));
    assert(near(m[0][1], m[1][0]));
    assert(m[0][0] > 0 && m[0][0] * m[1][1] - m[0][1] * m[1][0] > 0);
    assert(chain.inverse_dynamics(q, zero, zero, t, {0, -9.80665F, 0}));
    assert(near(t[0], 9.80665F * (1.5F * std::cos(q[0]) + .5F * std::cos(q[0] + q[1])), .001F));
    assert(near(t[1], 9.80665F * .5F * std::cos(q[0] + q[1]), .001F));
    Wrench wrench{1, 2, 0, 0, 0, .2F};
    auto jt = chain.transpose_wrench(j, wrench);
    assert(chain.inverse_dynamics(q, zero, zero, t, {0, 0, 0}, wrench));
    for (unsigned i = 0; i < 2; ++i)
        assert(near(t[i], jt[i]));
    float joint_power = 0, task_power = 0;
    for (unsigned i = 0; i < 2; ++i)
        joint_power += jt[i] * v[i];
    for (unsigned k = 0; k < 6; ++k)
        for (unsigned i = 0; i < 2; ++i)
            task_power += wrench[k] * j[k][i] * v[i];
    assert(near(joint_power, task_power));
    auto rot = orientation_error(rotation({1, 0, 0}, 3.14159265F), identity());
    assert(near(std::fabs(rot[0]), 3.14159265F, .001F));
    CartesianController<2> controller;
    CartesianController<2>::Config c;
    c.stiffness.fill(10);
    c.damping.fill(1);
    c.wrench_limit.fill(10);
    c.torque_limit.fill(2);
    c.torque_rate.fill(100);
    c.force_axes[1] = true;
    c.force_kp.fill(.1F);
    c.force_ki.fill(1);
    assert(controller.configure(chain, c, .001F));
    CartesianController<2>::Target target;
    target.pose = s.tool;
    target.pose.position[0] += .01F;
    auto result = controller.update(q, zero, target, {}, false);
    assert(result.valid);
    for (float x : result.torque)
        assert(std::fabs(x) <= .1001F);
    target.mode = CartesianMode::Hybrid;
    assert(!controller.update(q, zero, target, {}, false).valid);
    for (int i = 0; i < 10000; ++i)
    {
        result = controller.update(q, zero, target, {}, true);
        controller.track_applied({0, 0});
        assert(result.valid);
    }
    target.mode = CartesianMode::Admittance;
    assert(controller.update(q, zero, target, wrench, true).valid);
    Link slider;
    slider.type = JointType::Prismatic;
    slider.axis = {1, 0, 0};
    slider.mass = 2;
    SerialChain<1> prismatic;
    assert(prismatic.configure({slider}));
    std::array<float, 1> force;
    assert(prismatic.inverse_dynamics({.2F}, {0}, {3}, force, {0, 0, 0}));
    assert(near(force[0], 6));
}
static void test_voltage()
{
    using namespace algo::motor;
    CurrentLoopConfig c;
    c.d_axis.kp = c.q_axis.kp = 10;
    c.d_axis.sample_freq_hz = c.q_axis.sample_freq_hz = 20000;
    CurrentLoop loop;
    assert(loop.configure(c));
    CurrentLoopInput i;
    i.mode = CurrentLoopMode::Current;
    i.bus_voltage_v = 12;
    i.target_current_a = {10, 10};
    auto r = loop.update(i);
    assert(r.valid && r.voltage_scale < 1);
    assert(std::hypot(r.voltage_v.d, r.voltage_v.q) <= 12 * .577351F * .95F);
    c.decoupling = true;
    c.resistance_ohm = 1;
    c.ld_h = .001F;
    c.lq_h = .002F;
    c.flux_wb = .01F;
    assert(loop.configure(c));
    i.target_current_a = {0, 0};
    i.electrical_velocity_rad_s = 100;
    r = loop.update(i);
    assert(r.valid && near(r.voltage_v.q, 1));
}
static void test_identification()
{
    algo::motor::ElectricalIdentifier fit;
    constexpr float dt = 0.00005F, r = 1.2F, l = 0.001F, bias = 0.01F;
    fit.reset(dt);
    float current = 0;
    assert(fit.sample(current, 0, 0));
    for (unsigned n = 0; n < 10000; ++n)
    {
        const float v = ((n / 17) % 2 ? -.5F : .5F);
        current = ((v - bias) * dt + (l - r * dt * .5F) * current) / (l + r * dt * .5F);
        assert(fit.sample(current, v, 0));
    }
    auto result = fit.result();
    assert(result.valid);
    assert(near(result.resistance_ohm, r, .001F));
    assert(near(result.inductance_h, l, 1e-6F));
    assert(near(result.offset_v, bias, .001F));
    assert(!fit.sample(current, 0, 2));
    assert(!fit.result().valid);
    fit.reset(dt);
    for (int i = 0; i < 1000; ++i)
        fit.sample(0, 0, 0);
    assert(!fit.result().valid);
}
static void test_inertia_and_redundancy()
{
    JointInteraction c;
    InteractionConfig config;
    config.torque = {1, 0};
    config.desired_inertia = .1F;
    config.compensation.load_inertia = .2F;
    config.use_energy_tank = false;
    config.torque_rate_nm_s = 10000;
    config.stiffness_rate = 10000;
    config.elastic.observer_alpha = 1;
    assert(c.configure(config, 1000));
    JointFeedback f;
    f.external_valid = true;
    f.external_nm = .1F;
    JointTarget t;
    t.position_rad = .1F;
    t.stiffness = 1;
    assert(c.start(JointMode::InertiaShaping, t, f));
    auto r = c.update(f);
    assert(near(r.torque_nm, .3F));
    f.external_valid = false;
    assert(!c.update(f).valid);
    using namespace algo::robotics;
    SerialChain<7> chain;
    std::array<Link, 7> links{};
    assert(chain.configure(links));
    CartesianController<7> ctrl;
    CartesianController<7>::Config cfg;
    cfg.wrench_limit.fill(10);
    cfg.torque_limit.fill(10);
    cfg.torque_rate.fill(1e6F);
    cfg.posture_stiffness = 1;
    assert(ctrl.configure(chain, cfg, .001F));
    CartesianController<7>::Target target;
    target.posture[0] = .1F;
    auto out = ctrl.update({}, {}, target, {}, false);
    assert(out.valid);
    float task_leak = 0;
    for (float x : out.torque)
        task_leak += x;
    assert(std::fabs(task_leak) < 1e-5F);
    assert(out.torque[0] > .08F);
}
static void test_final_energy_and_reference_projection()
{
    JointInteraction ctrl;
    InteractionConfig c;
    c.torque = {1, 0};
    c.initial_energy_j = .01F;
    c.maximum_energy_j = .01F;
    c.torque_rate_nm_s = 10000;
    assert(ctrl.configure(c, 1000));
    JointTarget t;
    t.torque_nm = 1;
    JointFeedback f;
    f.velocity_rad_s = 1;
    assert(ctrl.start(JointMode::Torque, t, f));
    float work = 0;
    for (int i = 0; i < 1000; ++i)
    {
        assert(ctrl.update(f).valid);
        const float iq = ctrl.limit_applied(1, 1);
        work += iq * .001F;
        ctrl.track_applied(iq);
    }
    assert(work <= .01001F && ctrl.result().energy_j >= 0);
    ctrl.stop();
    c.use_energy_tank = false;
    c.servo_stiffness = 10;
    c.servo_damping = 1;
    c.admittance.force_deadband = 0;
    assert(ctrl.configure(c, 1000));
    assert(ctrl.set_position_limits(0, .01F));
    f = {};
    f.external_valid = true;
    f.external_nm = 1;
    assert(ctrl.start(JointMode::Drag, {}, f));
    JointResult r;
    for (int i = 0; i < 2000; ++i)
    {
        r = ctrl.update(f);
        ctrl.track_applied(r.iq_a);
        assert(r.reference_rad <= .01F);
    }
    f.external_nm = -1;
    for (int i = 0; i < 2000; ++i)
    {
        r = ctrl.update(f);
        ctrl.track_applied(r.iq_a);
    }
    assert(r.reference_rad < .0001F);
    algo::motor::ExecutionLimits limit;
    algo::motor::ExecutionConfig config;
    assert(limit.configure(config, 3, 20000, 4000));
    for (int i = 0; i < 25000; ++i)
        limit.current_sample(3, -1.5F, -1.5F, 0, 0, true);
    assert(limit.state().faults & uint32_t(algo::motor::ExecutionFault::I2t));
    assert(!limit.clear());
    const float debt = limit.state().excess_i2t_a2_s;
    assert(limit.configure(config, 3, 20000, 4000, true));
    assert(near(limit.state().excess_i2t_a2_s, debt));
    assert(limit.state().faults & uint32_t(algo::motor::ExecutionFault::I2t));
    assert(!limit.clear());
    for (int i = 0; i < 100000; ++i)
        limit.current_sample(0, 0, 0, 0, 0, false);
    assert(limit.clear());
}
int main()
{
    test_admittance();
    test_torque_and_interaction();
    test_limits();
    test_robotics();
    test_voltage();
    test_identification();
    test_inertia_and_redundancy();
    test_final_energy_and_reference_projection();
}
