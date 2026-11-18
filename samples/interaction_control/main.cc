#include <algo/robotics.h>
#include <cmath>
#include <cstdio>

// Two orthogonal slides with a spring contact on X; no device or transport.
int main()
{
    using namespace algo::robotics;
    using Controller = CartesianController<2>;
    Link x, y;
    x.type = y.type = JointType::Prismatic;
    x.axis = {1, 0, 0};
    y.axis = {0, 1, 0};
    x.mass = y.mass = 1;
    SerialChain<2> chain;
    if (!chain.configure({x, y}))
        return 1;
    Controller controller;
    Controller::Config config;
    config.stiffness.fill(100);
    config.damping.fill(25);
    config.force_kp.fill(.5F);
    config.force_ki.fill(2);
    config.wrench_limit.fill(10);
    config.torque_limit.fill(10);
    config.torque_rate.fill(1000);
    config.force_axes[0] = true;
    constexpr float dt = .001F;
    if (!controller.configure(chain, config, dt))
        return 2;
    Controller::Joints q{}, velocity{};
    Controller::Target target;
    target.mode = CartesianMode::Hybrid;
    target.pose.position = {0, .1F, 0};
    target.wrench[0] = 2;
    for (unsigned step = 0; step < 20000; ++step)
    {
        const float contact = -100 * std::max(0.0F, q[0]);
        auto result = controller.update(q, velocity, target, {contact, 0, 0, 0, 0, 0}, true);
        if (!result.valid)
            return 3;
        controller.track_applied(result.torque);
        velocity[0] += (result.torque[0] + contact) * dt / 2;
        velocity[1] += result.torque[1] * dt;
        for (unsigned i = 0; i < 2; ++i)
        {
            q[i] += velocity[i] * dt;
            if (!std::isfinite(q[i]))
                return 4;
        }
    }
    std::printf("contact N=%.4f, X m=%.4f, Y m=%.4f\n", 100 * q[0], q[0], q[1]);
    return std::fabs(100 * q[0] - 2) < .02F && std::fabs(q[1] - .1F) < .001F ? 0 : 5;
}
