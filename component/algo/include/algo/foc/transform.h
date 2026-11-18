#pragma once

namespace algo::motor::foc {

struct Phase3 {
    float u = 0.0F;
    float v = 0.0F;
    float w = 0.0F;
};

struct AlphaBeta {
    float alpha = 0.0F;
    float beta = 0.0F;
};

struct Dq {
    float d = 0.0F;
    float q = 0.0F;
};

AlphaBeta clarke_transform(const Phase3& input);
Phase3 inverse_clarke_transform(const AlphaBeta& input);
Dq park_transform(const AlphaBeta& input, float position_rad);
AlphaBeta inverse_park_transform(const Dq& input, float position_rad);

}  // namespace algo::motor::foc
