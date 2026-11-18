#pragma once

namespace algo::motor::controller {

struct PidSysConfig {
    float kp = 0.0F;
    float integrator_cutoff_hz = 0.0F;
    float zero_freq_hz = 0.0F;
    float pole_freq_hz = 0.0F;
    float sample_freq_hz = 0.0F;
    float integrator_limit = 0.0F;
    float output_limit = 0.0F;
    bool anti_windup = false;
};

class PidSys {
public:
    bool configure(const PidSysConfig& config);
    void reset();
    float update(float error);
    bool track_output(float applied_output);

    float proportional() const { return kp_out_; }
    float integrator_output() const { return ki_out_; }
    float lead_lag_output() const { return lead_lag_out_; }
    float output() const { return output_; }
    bool configured() const { return configured_; }

private:
    float kp_ = 0.0F;
    float integrator_limit_ = 0.0F;
    float output_limit_ = 0.0F;
    float wi_ = 0.0F;
    float ts_ = 0.0F;
    float lead_lag_num_[2] {};
    float lead_lag_den_[2] {};
    float last_input_ = 0.0F;
    float last_lead_lag_out_ = 0.0F;
    float integrator_state_ = 0.0F;
    float kp_out_ = 0.0F;
    float ki_out_ = 0.0F;
    float lead_lag_out_ = 0.0F;
    float output_ = 0.0F;
    float unlimited_output_ = 0.0F;
    bool anti_windup_ = false;
    bool lead_lag_enabled_ = false;
    bool configured_ = false;
};

}  // namespace algo::motor::controller
