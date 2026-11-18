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
    float& operator[](unsigned index) { return index==0U?x:index==1U?y:z; }
    const float& operator[](unsigned index) const { return index==0U?x:index==1U?y:z; }
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
    auto& operator[](unsigned row) { return value[row]; }
    const auto& operator[](unsigned row) const { return value[row]; }
};

} // namespace algo
