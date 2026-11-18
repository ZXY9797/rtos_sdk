#pragma once

#include <cstdint>

namespace foc {

struct Vec2 {
    float a, b;
};

struct Vec3 {
    float u, v, w;
};

struct Polar {
    float d, q;
};

struct SinCos {
    float sin, cos;
};

enum class MotorState : uint8_t {
    Idle,
    Calibrating,
    Aligning,
    OpenLoop,
    Tracking,
    Running,
    Error
};

enum class SensorMode : uint8_t {
    Sensorless,
    Hall,
    OpenLoop,
    Encoder
};

enum class ControlMode : uint8_t {
    Torque,
    Speed,
    Duty,
    Position
};

enum class ObserverType : uint8_t {
    MxLemming,
    MxLemmingLambda,
    Ortega,
    Pll
};

} // namespace foc
