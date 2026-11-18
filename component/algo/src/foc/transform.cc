#include <algo/foc/transform.h>

#include <algo/math/basic_math.h>

#include <math.h>

namespace algo::motor::foc {

AlphaBeta clarke_transform(const Phase3& input)
{
    AlphaBeta output {};
    output.alpha =
        (2.0F / 3.0F) * (input.u - (0.5F * (input.v + input.w)));
    output.beta =
        (2.0F / 3.0F) *
        (basic_math::kSqrt3Over2 * input.v -
         basic_math::kSqrt3Over2 * input.w);
    return output;
}

Phase3 inverse_clarke_transform(const AlphaBeta& input)
{
    Phase3 output {};
    output.u = input.alpha;
    output.v = (-input.alpha + (basic_math::kSqrt3 * input.beta)) * 0.5F;
    output.w = (-input.alpha - (basic_math::kSqrt3 * input.beta)) * 0.5F;
    return output;
}

Dq park_transform(const AlphaBeta& input, float position_rad)
{
    const float cos_theta = cosf(position_rad);
    const float sin_theta = sinf(position_rad);
    Dq output {};
    output.d = (input.alpha * cos_theta) + (input.beta * sin_theta);
    output.q = (-input.alpha * sin_theta) + (input.beta * cos_theta);
    return output;
}

AlphaBeta inverse_park_transform(const Dq& input, float position_rad)
{
    const float cos_theta = cosf(position_rad);
    const float sin_theta = sinf(position_rad);
    AlphaBeta output {};
    output.alpha = (input.d * cos_theta) - (input.q * sin_theta);
    output.beta = (input.d * sin_theta) + (input.q * cos_theta);
    return output;
}

}  // namespace algo::motor::foc
