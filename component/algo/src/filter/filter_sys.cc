#include <algo/filter/filter_sys.h>

#include <algo/math/basic_math.h>

#include <math.h>

namespace algo::motor::filter {

namespace {

constexpr float kComplexEps = 1.0e-10F;
constexpr float kCheby2RsDefault = 20.0F;
constexpr float kCheby2LpFstRatio = 1.5F;
constexpr float kCheby2HpFstRatio = 0.6F;

struct Complex {
    float re;
    float im;
};

Complex MakeComplex(float re, float im)
{
    return {re, im};
}

Complex Add(Complex a, Complex b)
{
    return MakeComplex(a.re + b.re, a.im + b.im);
}

Complex Sub(Complex a, Complex b)
{
    return MakeComplex(a.re - b.re, a.im - b.im);
}

Complex Mul(Complex a, Complex b)
{
    return MakeComplex((a.re * b.re) - (a.im * b.im),
                       (a.re * b.im) + (a.im * b.re));
}

Complex Div(Complex a, Complex b)
{
    const float den = (b.re * b.re) + (b.im * b.im);
    if (den <= kComplexEps) {
        return MakeComplex(0.0F, 0.0F);
    }
    return MakeComplex(((a.re * b.re) + (a.im * b.im)) / den,
                       ((a.im * b.re) - (a.re * b.im)) / den);
}

Complex Scale(Complex a, float scale)
{
    return MakeComplex(a.re * scale, a.im * scale);
}

float Abs2(Complex a)
{
    return (a.re * a.re) + (a.im * a.im);
}

Complex ProductNeg(const Complex* roots, int count)
{
    Complex product = MakeComplex(1.0F, 0.0F);
    for (int i = 0; i < count; ++i) {
        product = Mul(product, MakeComplex(-roots[i].re, -roots[i].im));
    }
    return product;
}

bool PolyFromRootsZInv(const Complex* roots, int count, float gain, Complex* poly)
{
    if ((count < 0) || (count > kMaxOrder)) {
        return false;
    }
    for (uint8_t i = 0; i <= kMaxOrder; ++i) {
        poly[i] = MakeComplex(0.0F, 0.0F);
    }
    poly[0] = MakeComplex(gain, 0.0F);
    for (int r = 0; r < count; ++r) {
        for (int i = r + 1; i >= 1; --i) {
            poly[i] = Sub(poly[i], Mul(roots[r], poly[i - 1]));
        }
    }
    return true;
}

bool DesignButterAnalog(int order, Complex* z, int* z_count,
                        Complex* p, int* p_count, float* k)
{
    if ((order <= 0) || (order > kMaxOrder) || (z == nullptr) ||
        (z_count == nullptr) || (p == nullptr) || (p_count == nullptr) ||
        (k == nullptr)) {
        return false;
    }

    (void)z;
    *z_count = 0;
    *p_count = order;
    for (int i = 0; i < order; ++i) {
        const float theta =
            ((2.0F * i + 1.0F + order) * basic_math::kPi) /
            (2.0F * order);
        p[i] = MakeComplex(cosf(theta), sinf(theta));
    }
    *k = ProductNeg(p, *p_count).re;
    return true;
}

bool DesignCheby2Analog(int order, float rs_db, Complex* z, int* z_count,
                        Complex* p, int* p_count, float* k)
{
    if ((order <= 0) || (order > kMaxOrder) || (z == nullptr) ||
        (z_count == nullptr) || (p == nullptr) || (p_count == nullptr) ||
        (k == nullptr)) {
        return false;
    }

    const float rs = (rs_db > 0.0F) ? rs_db : kCheby2RsDefault;
    const float de = 1.0F / sqrtf(powf(10.0F, 0.1F * rs) - 1.0F);
    if (!basic_math::is_finite_float(de) || (de <= 0.0F)) {
        return false;
    }
    const float mu = asinhf(1.0F / de) / order;
    if (!basic_math::is_finite_float(mu)) {
        return false;
    }

    *p_count = order;
    *z_count = 0;
    for (int i = 0; i < order; ++i) {
        const int m = -order + 1 + (2 * i);
        const float theta = (static_cast<float>(m) * basic_math::kPi) /
                            (2.0F * order);
        const Complex base = MakeComplex(-cosf(theta), -sinf(theta));
        const Complex tmp =
            MakeComplex(sinhf(mu) * base.re, coshf(mu) * base.im);
        if (Abs2(tmp) <= kComplexEps) {
            return false;
        }
        p[i] = Div(MakeComplex(1.0F, 0.0F), tmp);
    }

    for (int i = 0; i < order; ++i) {
        const int m = -order + 1 + (2 * i);
        const float theta = (static_cast<float>(m) * basic_math::kPi) /
                            (2.0F * order);
        const float sin_theta = sinf(theta);
        if (fabsf(sin_theta) <= kComplexEps) {
            continue;
        }
        z[*z_count] = MakeComplex(0.0F, 1.0F / sin_theta);
        ++(*z_count);
    }

    const Complex prod_p = ProductNeg(p, *p_count);
    const Complex prod_z = ProductNeg(z, *z_count);
    *k = (*z_count > 0) ? Div(prod_p, prod_z).re : prod_p.re;
    return true;
}

bool AnalogLpHpTransform(const FilterSysConfig& config,
                         Complex* z, int* z_count,
                         Complex* p, int* p_count, float* k)
{
    if ((z == nullptr) || (z_count == nullptr) || (p == nullptr) ||
        (p_count == nullptr) || (k == nullptr)) {
        return false;
    }

    const float fs = config.sample_freq_hz;
    const float fc = config.freq_hz;
    if ((fs <= 0.0F) || (fc <= 0.0F) || (fc >= 0.5F * fs)) {
        return false;
    }
    const float wa = 2.0F * fs * tanf((basic_math::kPi * fc) / fs);
    if (!basic_math::is_finite_float(wa) || (wa <= 0.0F)) {
        return false;
    }

    if (config.pass_type == FilterPassType::LowPass) {
        const int degree = *p_count - *z_count;
        for (int i = 0; i < *z_count; ++i) {
            z[i] = Scale(z[i], wa);
        }
        for (int i = 0; i < *p_count; ++i) {
            p[i] = Scale(p[i], wa);
        }
        *k *= powf(wa, degree);
        return true;
    }

    if (config.pass_type != FilterPassType::HighPass) {
        return false;
    }

    Complex old_z[kMaxOrder] {};
    Complex old_p[kMaxOrder] {};
    const int old_z_count = *z_count;
    const int old_p_count = *p_count;
    if ((old_z_count > kMaxOrder) || (old_p_count > kMaxOrder)) {
        return false;
    }
    for (int i = 0; i < old_z_count; ++i) {
        old_z[i] = z[i];
    }
    for (int i = 0; i < old_p_count; ++i) {
        old_p[i] = p[i];
    }

    for (int i = 0; i < old_z_count; ++i) {
        if (Abs2(old_z[i]) <= kComplexEps) {
            return false;
        }
        z[i] = Div(MakeComplex(wa, 0.0F), old_z[i]);
    }
    for (int i = 0; i < old_p_count; ++i) {
        if (Abs2(old_p[i]) <= kComplexEps) {
            return false;
        }
        p[i] = Div(MakeComplex(wa, 0.0F), old_p[i]);
    }

    const int degree = old_p_count - old_z_count;
    if ((degree < 0) || ((old_z_count + degree) > kMaxOrder)) {
        return false;
    }
    for (int i = 0; i < degree; ++i) {
        z[old_z_count + i] = MakeComplex(0.0F, 0.0F);
    }
    *z_count = old_z_count + degree;
    *p_count = old_p_count;
    *k *= Div(ProductNeg(old_z, old_z_count),
              ProductNeg(old_p, old_p_count)).re;
    return true;
}

bool BilinearZpk(float fs, Complex* z, int* z_count,
                 Complex* p, int p_count, float* k)
{
    if ((fs <= 0.0F) || (z == nullptr) || (z_count == nullptr) ||
        (p == nullptr) || (k == nullptr) || (p_count <= 0)) {
        return false;
    }

    const int degree = p_count - *z_count;
    if ((degree < 0) || ((*z_count + degree) > kMaxOrder)) {
        return false;
    }

    const float fs2 = 2.0F * fs;
    Complex num_prod = MakeComplex(1.0F, 0.0F);
    Complex den_prod = MakeComplex(1.0F, 0.0F);
    for (int i = 0; i < *z_count; ++i) {
        num_prod = Mul(num_prod, Sub(MakeComplex(fs2, 0.0F), z[i]));
    }
    for (int i = 0; i < p_count; ++i) {
        den_prod = Mul(den_prod, Sub(MakeComplex(fs2, 0.0F), p[i]));
    }
    *k *= Div(num_prod, den_prod).re;

    for (int i = 0; i < *z_count; ++i) {
        z[i] = Div(Add(MakeComplex(fs2, 0.0F), z[i]),
                   Sub(MakeComplex(fs2, 0.0F), z[i]));
    }
    for (int i = 0; i < p_count; ++i) {
        p[i] = Div(Add(MakeComplex(fs2, 0.0F), p[i]),
                   Sub(MakeComplex(fs2, 0.0F), p[i]));
    }
    for (int i = 0; i < degree; ++i) {
        z[*z_count + i] = MakeComplex(-1.0F, 0.0F);
    }
    *z_count += degree;
    return true;
}

bool ZpkToConfig(Order order, const Complex* z, int z_count,
                 const Complex* p, int p_count, float k,
                 TransferFunctionConfig& config)
{
    const int n = static_cast<int>(order);
    Complex b_poly[kMaxOrder + 1] {};
    Complex a_poly[kMaxOrder + 1] {};
    if ((z == nullptr) || (p == nullptr) || (n <= 0) || (n > kMaxOrder) ||
        (p_count != n) || (z_count > kMaxOrder) ||
        !PolyFromRootsZInv(z, z_count, k, b_poly) ||
        !PolyFromRootsZInv(p, p_count, 1.0F, a_poly)) {
        return false;
    }

    const float a0 = a_poly[0].re;
    if (fabsf(a0) <= kComplexEps) {
        return false;
    }

    config = TransferFunctionConfig {};
    config.order = order;
    for (int i = 0; i <= n; ++i) {
        const float b_real = b_poly[i].re / a0;
        const float a_real = a_poly[i].re / a0;
        const float b_imag = b_poly[i].im / a0;
        const float a_imag = a_poly[i].im / a0;
        if (!basic_math::is_finite_float(b_real) ||
            !basic_math::is_finite_float(a_real) ||
            (fabsf(b_imag) > 1.0e-5F) ||
            (fabsf(a_imag) > 1.0e-5F)) {
            return false;
        }
        config.num[i] = b_real;
        config.den[i] = a_real;
    }
    config.den[0] = 1.0F;
    return true;
}

}  // namespace

bool FilterSys::configure(const FilterSysConfig& config)
{
    FilterSysConfig design_config = config;
    Complex z[kMaxOrder] {};
    Complex p[kMaxOrder] {};
    int z_count = 0;
    int p_count = 0;
    float k = 0.0F;
    const int order = static_cast<int>(config.order);

    if ((order <= 0) || (order > kMaxOrder) ||
        (config.sample_freq_hz <= 0.0F) || (config.freq_hz <= 0.0F) ||
        !basic_math::is_finite_float(config.sample_freq_hz) ||
        !basic_math::is_finite_float(config.freq_hz) ||
        !basic_math::is_finite_float(config.attenuation_db)) {
        configured_ = false;
        return false;
    }

    if (config.proto == FilterProto::Cheby2) {
        const float nyquist = 0.5F * design_config.sample_freq_hz;
        if (config.pass_type == FilterPassType::LowPass) {
            design_config.freq_hz = config.freq_hz * kCheby2LpFstRatio;
            if (design_config.freq_hz > (0.99F * nyquist)) {
                design_config.freq_hz = 0.99F * nyquist;
            }
        } else if (config.pass_type == FilterPassType::HighPass) {
            design_config.freq_hz = config.freq_hz * kCheby2HpFstRatio;
            if (design_config.freq_hz < 1.0F) {
                design_config.freq_hz = 1.0F;
            }
        } else {
            configured_ = false;
            return false;
        }
    }

    if ((design_config.freq_hz <= 0.0F) ||
        (design_config.freq_hz >= (0.5F * design_config.sample_freq_hz))) {
        configured_ = false;
        return false;
    }

    const bool analog_ok =
        (config.proto == FilterProto::Butter)
            ? DesignButterAnalog(order, z, &z_count, p, &p_count, &k)
            : DesignCheby2Analog(order, config.attenuation_db,
                                 z, &z_count, p, &p_count, &k);
    if (!analog_ok ||
        !AnalogLpHpTransform(design_config, z, &z_count, p, &p_count, &k) ||
        !BilinearZpk(design_config.sample_freq_hz, z, &z_count, p, p_count, &k)) {
        configured_ = false;
        return false;
    }

    TransferFunctionConfig tf_config {};
    if (!ZpkToConfig(config.order, z, z_count, p, p_count, k, tf_config) ||
        !tf_.configure(tf_config)) {
        configured_ = false;
        return false;
    }

    design_config_ = config;
    transfer_config_ = tf_config;
    configured_ = true;
    return true;
}

void FilterSys::reset()
{
    tf_.reset();
}

float FilterSys::update(float input)
{
    return configured_ ? tf_.update(input) : 0.0F;
}

}  // namespace algo::motor::filter
