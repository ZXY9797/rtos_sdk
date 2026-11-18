#include <algo/trajectory/hermite_interp.h>

#include <algo/math/basic_math.h>

namespace algo::motor::trajectory {

namespace {

bool is_valid_segment(const CubicHermiteSegment& segment)
{
    return (segment.duration_s > 0.0F) &&
           basic_math::is_finite_float(segment.position_start) &&
           basic_math::is_finite_float(segment.velocity_start) &&
           basic_math::is_finite_float(segment.position_end) &&
           basic_math::is_finite_float(segment.velocity_end) &&
           basic_math::is_finite_float(segment.duration_s);
}

}  // namespace

bool cubic_hermite_basis(float ratio, CubicHermiteBasis* basis)
{
    if ((basis == nullptr) || !basic_math::is_finite_float(ratio)) {
        return false;
    }

    const float s = basic_math::clamp_float(ratio, 0.0F, 1.0F);
    const float s2 = s * s;
    const float s3 = s2 * s;

    basis->h00 = (2.0F * s3) - (3.0F * s2) + 1.0F;
    basis->h10 = s3 - (2.0F * s2) + s;
    basis->h01 = (-2.0F * s3) + (3.0F * s2);
    basis->h11 = s3 - s2;

    basis->dh00 = (6.0F * s2) - (6.0F * s);
    basis->dh10 = (3.0F * s2) - (4.0F * s) + 1.0F;
    basis->dh01 = (-6.0F * s2) + (6.0F * s);
    basis->dh11 = (3.0F * s2) - (2.0F * s);
    return true;
}

bool cubic_hermite_interpolate(const CubicHermiteSegment& segment,
                               float elapsed_s,
                               CubicHermiteSample* sample)
{
    if ((sample == nullptr) ||
        !is_valid_segment(segment) ||
        !basic_math::is_finite_float(elapsed_s)) {
        return false;
    }

    const float elapsed_clamped =
        basic_math::clamp_float(elapsed_s, 0.0F, segment.duration_s);
    const float ratio = elapsed_clamped / segment.duration_s;

    CubicHermiteBasis basis {};
    if (!cubic_hermite_basis(ratio, &basis)) {
        return false;
    }

    const float duration = segment.duration_s;
    sample->ratio = ratio;
    sample->basis = basis;
    sample->position =
        (basis.h00 * segment.position_start) +
        (basis.h10 * duration * segment.velocity_start) +
        (basis.h01 * segment.position_end) +
        (basis.h11 * duration * segment.velocity_end);
    sample->velocity =
        ((basis.dh00 * segment.position_start) / duration) +
        (basis.dh10 * segment.velocity_start) +
        ((basis.dh01 * segment.position_end) / duration) +
        (basis.dh11 * segment.velocity_end);

    return basic_math::is_finite_float(sample->position) &&
           basic_math::is_finite_float(sample->velocity);
}

float linear_interpolate(float start, float end, float ratio)
{
    const float s = basic_math::is_finite_float(ratio)
                        ? basic_math::clamp_float(ratio, 0.0F, 1.0F)
                        : 0.0F;
    return start + ((end - start) * s);
}

}  // namespace algo::motor::trajectory
