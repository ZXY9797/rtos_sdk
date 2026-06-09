#pragma once

#include "types.h"

namespace foc {

class FluxObserver {
public:
    virtual ~FluxObserver() = default;

    virtual void update(const Vec2 &v_ab, const Vec2 &i_ab, float dt) = 0;
    virtual uint16_t angle() const = 0;
    virtual float flux_magnitude() const = 0;
    virtual float bemf_d() const = 0;
    virtual float bemf_q() const = 0;
    virtual float speed_estimate() const = 0;
};

// MxLemming 磁链观测器
class MxLemmingObserver : public FluxObserver {
public:
    struct Config {
        float gain {0.0f};        // 观测器增益
        float lambda_sq {0.0f};   // 磁链幅值平方
    };

    MxLemmingObserver();
    explicit MxLemmingObserver(const Config &cfg);

    void update(const Vec2 &v_ab, const Vec2 &i_ab, float dt) override;
    uint16_t angle() const override;
    float flux_magnitude() const override;
    float bemf_d() const override;
    float bemf_q() const override;
    float speed_estimate() const override;

    void set_params(float rs, float ld, float lq);

private:
    Config cfg_;
    float rs_ {0.05f}, ld_ {0.0001f}, lq_ {0.0001f};
    float psi_alpha_ {0.0f}, psi_beta_ {0.0f};
    float e_alpha_ {0.0f}, e_beta_ {0.0f};
    float flux_mag_ {0.0f};
    float speed_est_ {0.0f};
    uint16_t angle_ {0};
};

// Ortega 磁链观测器
class OrtegaObserver : public FluxObserver {
public:
    struct Config {
        float gain {0.0f};
    };

    OrtegaObserver();
    explicit OrtegaObserver(const Config &cfg);

    void update(const Vec2 &v_ab, const Vec2 &i_ab, float dt) override;
    uint16_t angle() const override;
    float flux_magnitude() const override;
    float bemf_d() const override;
    float bemf_q() const override;
    float speed_estimate() const override;

    void set_params(float rs, float ld, float lq);

private:
    Config cfg_;
    float rs_ {0.05f}, ld_ {0.0001f}, lq_ {0.0001f};
    float psi_alpha_ {0.0f}, psi_beta_ {0.0f};
    float e_alpha_ {0.0f}, e_beta_ {0.0f};
    float flux_mag_ {0.0f};
    float speed_est_ {0.0f};
    uint16_t angle_ {0};
};

} // namespace foc
