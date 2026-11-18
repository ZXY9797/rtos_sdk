#pragma once

namespace algo {

struct LeadLagConfig {
    float sample_frequency_hz {1000.0F};
    float zero_frequency_hz {100.0F};
    float pole_frequency_hz {200.0F};
    float gain {1.0F};
};

class LeadLagCompensator {
public:
    LeadLagCompensator() = default;

    [[nodiscard]] bool configure(const LeadLagConfig &config);
    void bypass();
    [[nodiscard]] bool update(float input, float &output);
    void reset();

    [[nodiscard]] bool is_configured() const { return is_configured_; }

private:
    float b0_ {1.0F};
    float b1_ {0.0F};
    float a1_ {0.0F};
    float previous_input_ {0.0F};
    float previous_output_ {0.0F};
    bool is_configured_ {true};
};

[[nodiscard]] bool valid_lead_lag_config(const LeadLagConfig &config);

} // namespace algo
