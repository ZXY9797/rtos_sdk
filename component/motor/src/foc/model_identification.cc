#include <algo/foc/model_identification.h>
#include <algorithm>
#include <cmath>
namespace algo::motor
{
void ElectricalIdentifier::reset(float dt)
{
    *this = {};
    dt_ = dt;
}
void ElectricalIdentifier::accumulate(unsigned i, float v)
{
    const float y = v - corrections_[i], t = sums_[i] + y;
    corrections_[i] = (t - sums_[i]) - y;
    sums_[i] = t;
}
bool ElectricalIdentifier::sample(float i, float voltage, float velocity)
{
    if (!std::isfinite(dt_) || dt_ <= 0 || !std::isfinite(i) || !std::isfinite(voltage) ||
        !std::isfinite(velocity) || std::fabs(velocity) > 1.0F || count_ == UINT32_MAX)
    {
        rejected_ = true;
        return false;
    }
    if (!primed_)
    {
        previous_i_ = i;
        primed_ = true;
        return true;
    }
    const float x[3]{.5F * (i + previous_i_), 1e-4F * (i - previous_i_) / dt_, 1};
    previous_i_ = i;
    unsigned k = 0;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = r; c < 3; ++c)
            accumulate(k++, x[r] * x[c]);
    for (unsigned r = 0; r < 3; ++r)
        accumulate(6 + r, x[r] * voltage);
    accumulate(9, voltage * voltage);
    ++count_;
    return true;
}
ElectricalFit ElectricalIdentifier::result() const
{
    ElectricalFit result;
    result.samples = count_;
    if (rejected_ || count_ < 128)
        return result;
    float m[3][4]{};
    unsigned k = 0;
    for (unsigned r = 0; r < 3; ++r)
        for (unsigned c = r; c < 3; ++c)
            m[r][c] = m[c][r] = sums_[k++] / count_;
    for (unsigned r = 0; r < 3; ++r)
        m[r][3] = sums_[6 + r] / count_;
    for (unsigned p = 0; p < 3; ++p)
    {
        unsigned best = p;
        for (unsigned r = p + 1; r < 3; ++r)
            if (std::fabs(m[r][p]) > std::fabs(m[best][p]))
                best = r;
        if (std::fabs(m[best][p]) < 1e-6F)
            return result;
        for (unsigned c = 0; c < 4; ++c)
            std::swap(m[p][c], m[best][c]);
        const float pivot = m[p][p];
        for (unsigned c = p; c < 4; ++c)
            m[p][c] /= pivot;
        for (unsigned r = 0; r < 3; ++r)
            if (r != p)
            {
                const float factor = m[r][p];
                for (unsigned c = p; c < 4; ++c)
                    m[r][c] -= factor * m[p][c];
            }
    }
    result.resistance_ohm = m[0][3];
    result.inductance_h = m[1][3] * 1e-4F;
    result.offset_v = m[2][3];
    float sse = sums_[9];
    for (unsigned r = 0; r < 3; ++r)
        sse -= m[r][3] * sums_[6 + r];
    result.rms_v = std::sqrt(std::max(0.0F, sse / count_));
    result.valid = std::isfinite(result.rms_v) && result.resistance_ohm > 0 && result.resistance_ohm < 100 &&
                   result.inductance_h > 1e-7F && result.inductance_h < 1 && result.rms_v < 0.05F;
    return result;
}
} // namespace algo::motor
