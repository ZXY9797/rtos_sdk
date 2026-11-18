#pragma once

namespace algo::motor::trajectory {

struct CubicHermiteSegment {
    float position_start {0.0F};
    float velocity_start {0.0F};
    float position_end {0.0F};
    float velocity_end {0.0F};
    float duration_s {0.0F};
};

struct CubicHermiteBasis {
    float h00 {0.0F};
    float h10 {0.0F};
    float h01 {0.0F};
    float h11 {0.0F};
    float dh00 {0.0F};
    float dh10 {0.0F};
    float dh01 {0.0F};
    float dh11 {0.0F};
};

struct CubicHermiteSample {
    float ratio {0.0F};
    float position {0.0F};
    float velocity {0.0F};
    CubicHermiteBasis basis {};
};

bool cubic_hermite_basis(float ratio, CubicHermiteBasis* basis);
bool cubic_hermite_interpolate(const CubicHermiteSegment& segment,
                               float elapsed_s,
                               CubicHermiteSample* sample);
float linear_interpolate(float start, float end, float ratio);

}  // namespace algo::motor::trajectory
