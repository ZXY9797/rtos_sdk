#pragma once

#include <cstdint>

namespace algo {

struct LeadLagConfig {
    float fs {1000.0f};
    float fz {100.0f};
    float fp {1000.0f};
    float gain {1.0f};
};

class LeadLagCompensator {
public:
    LeadLagCompensator() = default;

    void init(const LeadLagConfig &cfg);
    void set_params(float fz, float fp);
    float update(float x);
    void reset();

    float fz() const { return cfg_.fz; }
    float fp() const { return cfg_.fp; }

private:
    LeadLagConfig cfg_;
    float b0_{0}, b1_{0}, a1_{0};
    float x1_{0}, y1_{0};

    void calculate_coefficients();
};

} // namespace algo
