#pragma once
#include <algo/robotics/serial_chain.h>
#include <algo/interaction/joint_interaction.h>

namespace algo::robotics
{
enum class CartesianMode
{
    Impedance,
    Admittance,
    Hybrid
};
template <size_t N> class CartesianController
{
  public:
    using Chain = SerialChain<N>;
    using Joints = typename Chain::Joints;
    struct Config
    {
        Wrench stiffness{}, damping{}, force_kp{}, force_ki{}, wrench_limit{};
        Joints torque_limit{}, torque_rate{};
        std::array<bool, 6> force_axes{};
        std::array<interaction::AdmittanceConfig, 6> admittance{};
        bool dynamics_feedforward{false};
        float posture_stiffness{0}, posture_damping{0}, nullspace_damping{0.01F};
    };
    struct Target
    {
        Pose pose{};
        Wrench velocity{}, wrench{};
        CartesianMode mode{};
        Joints posture{};
    };
    struct Result
    {
        Joints torque{};
        Wrench error{}, command{};
        Pose reference{};
        bool valid{false};
    };
    bool configure(const Chain &chain, const Config &config, float dt)
    {
        if (!std::isfinite(dt) || dt <= 0 || !std::isfinite(config.posture_stiffness) ||
            config.posture_stiffness < 0 || !std::isfinite(config.posture_damping) ||
            config.posture_damping < 0 || !std::isfinite(config.nullspace_damping) ||
            config.nullspace_damping < .001F)
            return false;
        for (size_t i = 0; i < N; ++i)
            if (!std::isfinite(config.torque_limit[i]) || config.torque_limit[i] <= 0 ||
                !std::isfinite(config.torque_rate[i]) || config.torque_rate[i] <= 0)
                return false;
        for (unsigned i = 0; i < 6; ++i)
        {
            const float values[]{config.stiffness[i], config.damping[i], config.force_kp[i],
                                 config.force_ki[i], config.wrench_limit[i]};
            for (float x : values)
                if (!std::isfinite(x) || x < 0)
                    return false;
            if (config.wrench_limit[i] <= 0 || !admittance_[i].configure(config.admittance[i], dt))
                return false;
        }
        chain_ = chain;
        config_ = config;
        dt_ = dt;
        configured_ = true;
        reset();
        return true;
    }
    void reset()
    {
        integral_ = {};
        unlimited_ = {};
        applied_ = {};
        for (auto &a : admittance_)
            a.reset();
    }
    void track_applied(const Joints &torque)
    {
        for (float x : torque)
            if (!std::isfinite(x))
                return;
        applied_ = torque;
    }
    Result update(const Joints &q, const Joints &qd, const Target &target, const Wrench &measured,
                  bool wrench_valid)
    {
        Result r;
        if (!configured_ || !finite(target.pose.position) || !rotation_valid(target.pose.orientation) ||
            (target.mode != CartesianMode::Impedance && target.mode != CartesianMode::Admittance &&
             target.mode != CartesianMode::Hybrid))
            return r;
        for (float x : qd)
            if (!std::isfinite(x))
                return r;
        for (unsigned k = 0; k < 6; ++k)
            if (!std::isfinite(target.velocity[k]) || !std::isfinite(target.wrench[k]) ||
                (wrench_valid && !std::isfinite(measured[k])))
                return r;
        if (target.mode != CartesianMode::Impedance && !wrench_valid)
            return r;
        typename Chain::State state;
        if (!chain_.forward(q, state))
            return r;
        const auto j = chain_.jacobian(state);
        r.reference = target.pose;
        Wrench desired_v = target.velocity;
        if (target.mode == CartesianMode::Admittance)
        {
            Vec3 rotation_offset{};
            for (unsigned k = 0; k < 6; ++k)
            {
                interaction::MotionState motion;
                if (!admittance_[k].update(measured[k], -target.wrench[k], motion))
                    return {};
                if (k < 3)
                    r.reference.position[k] += motion.position;
                else
                    rotation_offset[k - 3] = motion.position;
                desired_v[k] += motion.velocity;
            }
            const float angle = std::sqrt(dot(rotation_offset, rotation_offset));
            if (angle > 1e-7F)
                r.reference.orientation =
                    mul(rotation(scale(rotation_offset, 1 / angle), angle), r.reference.orientation);
        }
        auto ep = sub(r.reference.position, state.tool.position);
        auto er = orientation_error(r.reference.orientation, state.tool.orientation);
        Wrench velocity{};
        for (unsigned k = 0; k < 6; ++k)
            for (size_t i = 0; i < N; ++i)
                velocity[k] += j[k][i] * qd[i];
        for (unsigned k = 0; k < 6; ++k)
        {
            r.error[k] = k < 3 ? ep[k] : er[k - 3];
            if (target.mode == CartesianMode::Hybrid && config_.force_axes[k])
            {
                const float e = target.wrench[k] + measured[k];
                integral_[k] += config_.force_ki[k] * e * dt_;
                unlimited_[k] = target.wrench[k] + config_.force_kp[k] * e + integral_[k] -
                                config_.damping[k] * velocity[k];
            }
            else
            {
                integral_[k] = 0;
                unlimited_[k] = config_.stiffness[k] * r.error[k] +
                                config_.damping[k] * (desired_v[k] - velocity[k]) + target.wrench[k];
            }
            r.command[k] = std::clamp(unlimited_[k], -config_.wrench_limit[k], config_.wrench_limit[k]);
            if (target.mode == CartesianMode::Hybrid && config_.force_axes[k])
                integral_[k] += r.command[k] - unlimited_[k];
        }
        auto control = Chain::transpose_wrench(j, r.command);
        if constexpr (N > 6)
        {
            if (config_.posture_stiffness > 0 || config_.posture_damping > 0)
            {
                Joints posture{};
                for (size_t i = 0; i < N; ++i)
                {
                    if (!std::isfinite(target.posture[i]))
                        return {};
                    posture[i] = config_.posture_stiffness * (target.posture[i] - q[i]) -
                                 config_.posture_damping * qd[i];
                }
                Wrench projected{};
                float lower[6][6]{};
                for (unsigned a = 0; a < 6; ++a)
                {
                    for (size_t i = 0; i < N; ++i)
                        projected[a] += j[a][i] * posture[i];
                    for (unsigned b = 0; b <= a; ++b)
                    {
                        float value = a == b ? config_.nullspace_damping * config_.nullspace_damping : 0;
                        for (size_t i = 0; i < N; ++i)
                            value += j[a][i] * j[b][i];
                        for (unsigned k = 0; k < b; ++k)
                            value -= lower[a][k] * lower[b][k];
                        if (a == b)
                        {
                            if (!std::isfinite(value) || value <= 0)
                                return {};
                            lower[a][b] = std::sqrt(value);
                        }
                        else
                            lower[a][b] = value / lower[b][b];
                    }
                }
                for (unsigned a = 0; a < 6; ++a)
                {
                    for (unsigned k = 0; k < a; ++k)
                        projected[a] -= lower[a][k] * projected[k];
                    projected[a] /= lower[a][a];
                }
                for (unsigned n = 6; n > 0; --n)
                {
                    const unsigned a = n - 1;
                    for (unsigned k = a + 1; k < 6; ++k)
                        projected[a] -= lower[k][a] * projected[k];
                    projected[a] /= lower[a][a];
                }
                const auto correction = Chain::transpose_wrench(j, projected);
                for (size_t i = 0; i < N; ++i)
                    control[i] += posture[i] - correction[i];
            }
        }
        Joints bias{};
        if (config_.dynamics_feedforward && !chain_.bias_dynamics(q, qd, bias))
            return {};
        float scale_factor = 1;
        for (size_t i = 0; i < N; ++i)
        {
            const float request = control[i] + bias[i];
            r.torque[i] = std::clamp(request, -config_.torque_limit[i], config_.torque_limit[i]);
            r.torque[i] = applied_[i] + std::clamp(r.torque[i] - applied_[i], -config_.torque_rate[i] * dt_,
                                                   config_.torque_rate[i] * dt_);
            r.torque[i] = std::clamp(r.torque[i], -config_.torque_limit[i], config_.torque_limit[i]);
            if (std::fabs(control[i]) > 1e-6F)
                scale_factor =
                    std::min(scale_factor, std::clamp((r.torque[i] - bias[i]) / control[i], 0.0F, 1.0F));
            if (!std::isfinite(r.torque[i]))
                return {};
        }
        // Conservative common-scale back calculation; avoids an inverse Jacobian at singularities.
        if (target.mode == CartesianMode::Hybrid)
            for (unsigned k = 0; k < 6; ++k)
                if (config_.force_axes[k])
                    integral_[k] = std::clamp(integral_[k] + (scale_factor - 1) * r.command[k],
                                              -config_.wrench_limit[k], config_.wrench_limit[k]);
        r.valid = true;
        return r;
    }

  private:
    Chain chain_{};
    Config config_{};
    float dt_{0};
    bool configured_{false};
    Wrench integral_{}, unlimited_{};
    Joints applied_{};
    std::array<interaction::Admittance, 6> admittance_{};
};
} // namespace algo::robotics
