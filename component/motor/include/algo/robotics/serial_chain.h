#pragma once
#include <array>
#include <algo/math/spatial_math.h>
#include <algorithm>
#include <cmath>
#include <cstddef>

namespace algo::robotics
{
using Vec3 = algo::Vector3;
using Mat3 = algo::Matrix3;
using Wrench = std::array<float, 6>; // environment ON tool, world force XYZ then moment XYZ
inline Vec3 add(Vec3 a, Vec3 b) { return spatial::add(a, b); }
inline Vec3 sub(Vec3 a, Vec3 b) { return spatial::subtract(a, b); }
inline Vec3 scale(Vec3 a, float s) { return spatial::scale(a, s); }
inline float dot(Vec3 a, Vec3 b) { return spatial::dot(a, b); }
inline Vec3 cross(Vec3 a, Vec3 b) { return spatial::cross(a, b); }
inline Mat3 identity() { return spatial::identity_matrix(); }
inline Vec3 mul(const Mat3 &a, Vec3 b) { return spatial::multiply(a, b); }
inline Mat3 transpose(const Mat3 &a) { return spatial::transpose(a); }
inline Mat3 mul(const Mat3 &a, const Mat3 &b) { return spatial::multiply(a, b); }
inline Vec3 row(const Mat3 &a, unsigned i) { return {a[i][0], a[i][1], a[i][2]}; }
inline bool finite(Vec3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline bool rotation_valid(const Mat3 &r)
{
    for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 3; ++j)
            if (!finite(row(r, i)) || std::fabs(dot(row(r, i), row(r, j)) - (i == j ? 1.0F : 0.0F)) > .001F)
                return false;
    return spatial::determinant(r) > .999F;
}
inline Mat3 rotation(Vec3 axis, float angle) { return spatial::axis_angle(axis, angle); }
struct Pose
{
    Mat3 orientation{identity()};
    Vec3 position{};
};
inline Pose compose(const Pose &a, const Pose &b)
{
    return {mul(a.orientation, b.orientation), add(a.position, mul(a.orientation, b.position))};
}
inline Vec3 orientation_error(const Mat3 &target, const Mat3 &actual)
{
    return spatial::rotation_vector(spatial::quaternion(mul(target, transpose(actual))));
}
enum class JointType
{
    Revolute,
    Prismatic
};
struct Link
{
    Pose parent_to_joint{}, joint_to_link{};
    Vec3 axis{0, 0, 1}, center_of_mass{};
    Mat3 inertia{{{0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}; // about COM, in link coordinates, kg m^2
    float mass{0}, rotor_inertia{0};                 // reflected to this joint
    JointType type{JointType::Revolute};
};

template <size_t N> class SerialChain
{
    static_assert(N > 0 && N <= 7, "Supported fixed joint count is 1..7");

  public:
    using Joints = std::array<float, N>;
    using Jacobian = std::array<Joints, 6>;
    using MassMatrix = std::array<Joints, N>;
    struct State
    {
        std::array<Pose, N> links{};
        std::array<Vec3, N> origins{}, axes{};
        Pose tool{};
    };
    bool configure(const std::array<Link, N> &links, Pose tool = {})
    {
        if (!finite(tool.position) || !rotation_valid(tool.orientation))
            return false;
        for (const auto &l : links)
        {
            if (!finite(l.axis) || std::fabs(dot(l.axis, l.axis) - 1) > 0.001F || !finite(l.center_of_mass) ||
                !finite(l.parent_to_joint.position) || !finite(l.joint_to_link.position) ||
                !rotation_valid(l.parent_to_joint.orientation) ||
                !rotation_valid(l.joint_to_link.orientation) || !std::isfinite(l.mass) || l.mass < 0 ||
                !std::isfinite(l.rotor_inertia) || l.rotor_inertia < 0 ||
                (l.type != JointType::Revolute && l.type != JointType::Prismatic))
                return false;
            for (unsigned i = 0; i < 3; ++i)
                for (unsigned j = 0; j < 3; ++j)
                    if (!finite(row(l.inertia, i)) || std::fabs(l.inertia[i][j] - l.inertia[j][i]) > 1e-6F)
                        return false;
            // Positive semidefinite inertia: all principal minors, not just diagonal entries.
            for (unsigned i = 0; i < 3; ++i)
            {
                if (l.inertia[i][i] < 0)
                    return false;
                for (unsigned j = i + 1; j < 3; ++j)
                    if (l.inertia[i][i] * l.inertia[j][j] - l.inertia[i][j] * l.inertia[i][j] < -1e-8F)
                        return false;
            }
            if (spatial::determinant(l.inertia) < -1e-8F)
                return false;
        }
        links_ = links;
        tool_ = tool;
        configured_ = true;
        return true;
    }
    bool forward(const Joints &q, State &state) const
    {
        if (!configured_)
            return false;
        Pose parent{};
        for (size_t i = 0; i < N; ++i)
        {
            if (!std::isfinite(q[i]))
                return false;
            Pose joint = compose(parent, links_[i].parent_to_joint);
            state.origins[i] = joint.position;
            state.axes[i] = mul(joint.orientation, links_[i].axis);
            Pose motion{};
            if (links_[i].type == JointType::Revolute)
                motion.orientation = rotation(links_[i].axis, q[i]);
            else
                motion.position = scale(links_[i].axis, q[i]);
            parent = compose(compose(joint, motion), links_[i].joint_to_link);
            state.links[i] = parent;
        }
        state.tool = compose(parent, tool_);
        return true;
    }
    Jacobian jacobian(const State &state) const
    {
        Jacobian j{};
        for (size_t i = 0; i < N; ++i)
        {
            const bool rev = links_[i].type == JointType::Revolute;
            const Vec3 linear =
                rev ? cross(state.axes[i], sub(state.tool.position, state.origins[i])) : state.axes[i];
            for (unsigned k = 0; k < 3; ++k)
            {
                j[k][i] = linear[k];
                j[k + 3][i] = rev ? state.axes[i][k] : 0;
            }
        }
        return j;
    }
    static Joints transpose_wrench(const Jacobian &j, const Wrench &wrench)
    {
        Joints t{};
        for (size_t i = 0; i < N; ++i)
            for (unsigned k = 0; k < 6; ++k)
                t[i] += j[k][i] * wrench[k];
        return t;
    }
    // Recursive Newton-Euler, world-frame vectors. Tool wrench is resistance to be supplied.
    bool inverse_dynamics(const Joints &q, const Joints &qd, const Joints &qdd, Joints &torque,
                          Vec3 gravity = {0, 0, -9.80665F}, Wrench resistance = {}) const
    {
        State state;
        if (!finite(gravity) || !forward(q, state))
            return false;
        for (float x : resistance)
            if (!std::isfinite(x))
                return false;
        std::array<Vec3, N> omega{}, alpha{}, accel{}, force{}, moment{};
        Vec3 wp{}, ap{}, vp_acc{};
        Pose parent{};
        for (size_t i = 0; i < N; ++i)
        {
            if (!std::isfinite(qd[i]) || !std::isfinite(qdd[i]))
                return false;
            const auto &l = links_[i];
            const Vec3 axis = state.axes[i];
            const Vec3 pre = sub(state.origins[i], parent.position);
            Vec3 aj = add(vp_acc, add(cross(ap, pre), cross(wp, cross(wp, pre))));
            Vec3 joint = state.origins[i];
            if (l.type == JointType::Revolute)
            {
                omega[i] = add(wp, scale(axis, qd[i]));
                alpha[i] = add(ap, add(scale(axis, qdd[i]), cross(wp, scale(axis, qd[i]))));
            }
            else
            {
                omega[i] = wp;
                alpha[i] = ap;
                const Vec3 displacement = scale(axis, q[i]);
                joint = add(joint, displacement);
                aj = add(aj, add(cross(ap, displacement),
                                 add(cross(wp, cross(wp, displacement)),
                                     add(scale(cross(wp, axis), 2 * qd[i]), scale(axis, qdd[i])))));
            }
            const Vec3 offset = sub(state.links[i].position, joint);
            accel[i] = add(aj, add(cross(alpha[i], offset), cross(omega[i], cross(omega[i], offset))));
            const Vec3 com = mul(state.links[i].orientation, l.center_of_mass);
            const Vec3 ac = add(accel[i], add(cross(alpha[i], com), cross(omega[i], cross(omega[i], com))));
            force[i] = scale(sub(ac, gravity), l.mass);
            const Mat3 inertia =
                mul(mul(state.links[i].orientation, l.inertia), transpose(state.links[i].orientation));
            moment[i] = add(add(mul(inertia, alpha[i]), cross(omega[i], mul(inertia, omega[i]))),
                            cross(com, force[i]));
            wp = omega[i];
            ap = alpha[i];
            vp_acc = accel[i];
            parent = state.links[i];
        }
        Vec3 child_force{resistance[0], resistance[1], resistance[2]},
            child_moment{resistance[3], resistance[4], resistance[5]};
        Vec3 child_position = state.tool.position;
        for (size_t n = N; n > 0; --n)
        {
            const size_t i = n - 1;
            force[i] = add(force[i], child_force);
            moment[i] = add(moment[i], add(child_moment,
                                           cross(sub(child_position, state.links[i].position), child_force)));
            torque[i] =
                (links_[i].type == JointType::Prismatic
                     ? dot(state.axes[i], force[i])
                     : dot(state.axes[i],
                           add(moment[i], cross(sub(state.links[i].position, state.origins[i]), force[i])))) +
                links_[i].rotor_inertia * qdd[i];
            if (!std::isfinite(torque[i]))
                return false;
            child_force = force[i];
            child_moment = moment[i];
            child_position = state.links[i].position;
        }
        return true;
    }
    bool bias_dynamics(const Joints &q, const Joints &qd, Joints &torque,
                       Vec3 gravity = {0, 0, -9.80665F}) const
    {
        if constexpr (N == 1)
        {
            State s;
            if (!finite(gravity) || !std::isfinite(qd[0]) || !forward(q, s))
                return false;
            // One fixed-axis joint has constant generalized inertia and zero
            // Coriolis term. This is the closed form of the recursion above.
            const Vec3 force = scale(gravity, -links_[0].mass);
            const Vec3 com = add(s.links[0].position, mul(s.links[0].orientation, links_[0].center_of_mass));
            torque[0] = links_[0].type == JointType::Prismatic
                            ? dot(s.axes[0], force)
                            : dot(s.axes[0], cross(sub(com, s.origins[0]), force));
            return std::isfinite(torque[0]);
        }
        else
        {
            Joints zero{};
            return inverse_dynamics(q, qd, zero, torque, gravity);
        }
    }
    bool mass_matrix(const Joints &q, MassMatrix &mass) const
    {
        Joints zero{}, a{}, t{};
        for (size_t c = 0; c < N; ++c)
        {
            a = {};
            a[c] = 1;
            if (!inverse_dynamics(q, zero, a, t, {0, 0, 0}))
                return false;
            for (size_t r = 0; r < N; ++r)
                mass[r][c] = t[r];
        }
        return true;
    }

  private:
    std::array<Link, N> links_{};
    Pose tool_{};
    bool configured_{false};
};
} // namespace algo::robotics
