#include <algo/filter/notch_peak.h>

#include <algo/math/basic_math.h>

#include <math.h>

namespace algo::motor::filter {

namespace {

constexpr float kEps = 1.0e-10F;

}  // namespace

bool NotchPeak::configure(const NotchPeakConfig& config)
{
    if ((config.center_freq_hz <= 0.0F) ||
        (config.bandwidth_hz <= 0.0F) ||
        (config.sample_freq_hz <= 0.0F) ||
        (config.center_freq_hz >= 0.5F * config.sample_freq_hz) ||
        !((config.as_param > 1.0F) ||
          ((config.as_param > 0.0F) && (config.as_param < 1.0F))) ||
        !basic_math::is_finite_float(config.center_freq_hz) ||
        !basic_math::is_finite_float(config.bandwidth_hz) ||
        !basic_math::is_finite_float(config.as_param) ||
        !basic_math::is_finite_float(config.sample_freq_hz)) {
        configured_ = false;
        return false;
    }

    const float omega0 = basic_math::kTwoPi * config.center_freq_hz;
    float q = config.center_freq_hz / config.bandwidth_hz;
    if (!basic_math::is_finite_float(omega0) ||
        !basic_math::is_finite_float(q) || (q <= 0.0F)) {
        configured_ = false;
        return false;
    }
    if (q < 0.5F) {
        q = 0.5F;
    }

    float num_a2 = 1.0F;
    float num_a1 = 0.0F;
    const float num_a0 = omega0 * omega0;
    float den_a2 = 1.0F;
    float den_a1 = 0.0F;
    const float den_a0 = omega0 * omega0;

    if (config.as_param > 1.0F) {
        float zeta_z = powf(10.0F, -config.as_param / 20.0F) / (2.0F * q);
        if (!basic_math::is_finite_float(zeta_z) || (zeta_z < 0.0F)) {
            configured_ = false;
            return false;
        }
        if (zeta_z > 0.999F) {
            zeta_z = 0.999F;
        }
        num_a1 = 2.0F * zeta_z * omega0;
        den_a1 = omega0 / q;
    } else {
        const float a = 1.0F / sqrtf(config.as_param);
        if (!basic_math::is_finite_float(a) || (a <= 0.0F)) {
            configured_ = false;
            return false;
        }
        num_a1 = omega0 / q;
        den_a1 = omega0 / (q * a);
    }

    const float t = 1.0F / config.sample_freq_hz;
    const float omega_p = basic_math::kTwoPi * config.center_freq_hz;
    const float tan_item = tanf(omega_p * t * 0.5F);
    if (!basic_math::is_finite_float(t) ||
        !basic_math::is_finite_float(omega_p) ||
        !basic_math::is_finite_float(tan_item) ||
        (tan_item == 0.0F)) {
        configured_ = false;
        return false;
    }

    const float k = omega_p / tan_item;
    if (!basic_math::is_finite_float(k)) {
        configured_ = false;
        return false;
    }
    const float k2 = k * k;

    const float num_z2 = (num_a2 * k2) + (num_a1 * k) + num_a0;
    const float num_z1 = (-2.0F * num_a2 * k2) + (2.0F * num_a0);
    const float num_z0 = (num_a2 * k2) - (num_a1 * k) + num_a0;
    const float den_z2 = (den_a2 * k2) + (den_a1 * k) + den_a0;
    const float den_z1 = (-2.0F * den_a2 * k2) + (2.0F * den_a0);
    const float den_z0 = (den_a2 * k2) - (den_a1 * k) + den_a0;

    if (!basic_math::is_finite_float(den_z2) ||
        (fabsf(den_z2) <= kEps)) {
        configured_ = false;
        return false;
    }

    TransferFunctionConfig tf_config {};
    tf_config.order = Order::Second;
    tf_config.num[0] = num_z2 / den_z2;
    tf_config.num[1] = num_z1 / den_z2;
    tf_config.num[2] = num_z0 / den_z2;
    tf_config.den[0] = 1.0F;
    tf_config.den[1] = den_z1 / den_z2;
    tf_config.den[2] = den_z0 / den_z2;

    if (!basic_math::is_finite_float(tf_config.num[0]) ||
        !basic_math::is_finite_float(tf_config.num[1]) ||
        !basic_math::is_finite_float(tf_config.num[2]) ||
        !basic_math::is_finite_float(tf_config.den[1]) ||
        !basic_math::is_finite_float(tf_config.den[2]) ||
        !tf_.configure(tf_config)) {
        configured_ = false;
        return false;
    }

    design_config_ = config;
    transfer_config_ = tf_config;
    configured_ = true;
    return true;
}

void NotchPeak::reset()
{
    tf_.reset();
}

float NotchPeak::update(float input)
{
    return configured_ ? tf_.update(input) : 0.0F;
}

}  // namespace algo::motor::filter
