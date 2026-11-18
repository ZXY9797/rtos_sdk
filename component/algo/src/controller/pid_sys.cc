#include <algo/controller/pid_sys.h>

#include <algo/math/basic_math.h>

namespace algo::motor::controller {

bool PidSys::configure(const PidSysConfig& config)
{
    if ((config.sample_freq_hz <= 0.0F) ||
        (config.integrator_cutoff_hz < 0.0F) ||
        (config.zero_freq_hz < 0.0F) ||
        (config.pole_freq_hz < 0.0F) ||
        (config.integrator_limit < 0.0F) ||
        (config.output_limit < 0.0F) ||
        !basic_math::is_finite_float(config.kp) ||
        !basic_math::is_finite_float(config.integrator_cutoff_hz) ||
        !basic_math::is_finite_float(config.zero_freq_hz) ||
        !basic_math::is_finite_float(config.pole_freq_hz) ||
        !basic_math::is_finite_float(config.sample_freq_hz) ||
        !basic_math::is_finite_float(config.integrator_limit) ||
        !basic_math::is_finite_float(config.output_limit)) {
        configured_ = false;
        return false;
    }
    if (((config.zero_freq_hz > 0.0F) && (config.pole_freq_hz <= 0.0F)) ||
        ((config.zero_freq_hz <= 0.0F) && (config.pole_freq_hz > 0.0F))) {
        configured_ = false;
        return false;
    }

    kp_ = config.kp;
    integrator_limit_ = config.integrator_limit;
    output_limit_ = config.output_limit;
    anti_windup_ = config.anti_windup;
    wi_ = (config.integrator_cutoff_hz > 0.0F)
              ? (basic_math::kTwoPi * config.integrator_cutoff_hz)
              : 0.0F;
    ts_ = 1.0F / config.sample_freq_hz;
    lead_lag_num_[0] = 1.0F;
    lead_lag_num_[1] = 0.0F;
    lead_lag_den_[0] = 1.0F;
    lead_lag_den_[1] = 0.0F;
    lead_lag_enabled_ = false;

    if ((config.zero_freq_hz > 0.0F) &&
        (config.pole_freq_hz > 0.0F)) {
        const float wz = basic_math::kTwoPi * config.zero_freq_hz;
        const float wp = basic_math::kTwoPi * config.pole_freq_hz;
        const float zk = 2.0F * config.sample_freq_hz;
        const float den0 = zk + wp;
        if ((den0 == 0.0F) || !basic_math::is_finite_float(den0)) {
            configured_ = false;
            return false;
        }
        lead_lag_num_[0] = (zk + wz) / den0;
        lead_lag_num_[1] = (wz - zk) / den0;
        lead_lag_den_[0] = 1.0F;
        lead_lag_den_[1] = (wp - zk) / den0;
        lead_lag_enabled_ = true;
    }

    configured_ = true;
    reset();
    return true;
}

void PidSys::reset()
{
    last_input_ = 0.0F;
    last_lead_lag_out_ = 0.0F;
    integrator_state_ = 0.0F;
    kp_out_ = 0.0F;
    ki_out_ = 0.0F;
    lead_lag_out_ = 0.0F;
    output_ = 0.0F;
    unlimited_output_ = 0.0F;
}

bool PidSys::track_output(float applied)
{
    if (!configured_ || !basic_math::is_finite_float(applied)) return false;
    const float gain = kp_ * wi_;
    if (gain != 0.0F) {
        ki_out_ += applied - unlimited_output_;
        if (integrator_limit_ > 0.0F)
            ki_out_ = basic_math::clamp_float(ki_out_, -integrator_limit_, integrator_limit_);
        integrator_state_ = ki_out_ / gain;
    }
    output_ = unlimited_output_ = applied;
    return true;
}

float PidSys::update(float error)
{
    if (!configured_) {
        return 0.0F;
    }

    kp_out_ = kp_ * error;

    if (wi_ > 0.0F) {
        integrator_state_ += error * ts_;
        ki_out_ = kp_ * wi_ * integrator_state_;
        if (integrator_limit_ > 0.0F) {
            ki_out_ = basic_math::clamp_float(
                ki_out_, -integrator_limit_, integrator_limit_);
        }
    } else {
        integrator_state_ = 0.0F;
        ki_out_ = 0.0F;
    }

    if (lead_lag_enabled_) {
        lead_lag_out_ = (kp_ * lead_lag_num_[0] * error) +
                        (kp_ * lead_lag_num_[1] * last_input_) -
                        (lead_lag_den_[1] * last_lead_lag_out_);
        last_input_ = error;
        last_lead_lag_out_ = lead_lag_out_;
    } else {
        last_input_ = error;
        last_lead_lag_out_ = 0.0F;
        lead_lag_out_ = 0.0F;
    }

    unlimited_output_ = kp_out_ + ki_out_ + lead_lag_out_;
    output_ = unlimited_output_;
    if (output_limit_ > 0.0F) {
        output_ =
            basic_math::clamp_float(output_, -output_limit_, output_limit_);
    }
    if (anti_windup_) (void)track_output(output_);
    return output_;
}

}  // namespace algo::motor::controller
