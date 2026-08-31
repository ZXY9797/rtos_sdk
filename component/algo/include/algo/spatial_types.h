#pragma once

namespace algo {

struct Vector2 {
    float x {0.0F};
    float y {0.0F};
};

struct Vector3 {
    float x {0.0F};
    float y {0.0F};
    float z {0.0F};
};

struct Quaternion {
    float w {1.0F};
    float x {0.0F};
    float y {0.0F};
    float z {0.0F};
};

struct Matrix3 {
    float value[3][3] {
        {1.0F, 0.0F, 0.0F},
        {0.0F, 1.0F, 0.0F},
        {0.0F, 0.0F, 1.0F},
    };
};

} // namespace algo
