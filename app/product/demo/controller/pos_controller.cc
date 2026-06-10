#include "pos_controller.h"
#include <cmath>
#include <algorithm>

void PosController::init(const PidConfig &pid_cfg, const BacklashConfig &backlash_cfg,
                         float *output_angle_offset_table, uint32_t offset_table_size,
                         float loop_hz) {
    backlash_cfg_ = backlash_cfg;
    dz_cnt_max_ = static_cast<uint32_t>(backlash_cfg.dwell_time_s * loop_hz);

    pid_.init(pid_cfg);

    if (output_angle_offset_table && offset_table_size >= 3) {
        LagrangeConfig lcfg;
        lcfg.name = "MOTOR_OUT";
        lcfg.order = LagrangeOrder::Second;
        lcfg.x_min = 0.0f;
        lcfg.x_max = TWO_PI;
        lcfg.point_count = offset_table_size;
        output_interp_.init(lcfg, output_angle_offset_table);
        has_output_calib_ = true;
    }
}

void PosController::update_sensor(float v1_norm, float v2_norm) {
    angle_raw_ = std::atan2(v1_norm, v2_norm);
    if (angle_raw_ < 0.0f) angle_raw_ += TWO_PI;

    if (has_output_calib_) {
        float offset = 0.0f;
        if (output_interp_.fit(angle_raw_, &offset)) {
            angle_calib_ = angle_raw_ + offset;
        } else {
            angle_calib_ = angle_raw_;
        }
    } else {
        angle_calib_ = angle_raw_;
    }

    if (has_zero_calib_) {
        angle_zero_ = angle_calib_ - zero_offset_;
        if (angle_zero_ < -TWO_PI * 0.01f) angle_zero_ += TWO_PI;
    } else {
        angle_zero_ = angle_calib_;
    }

    if (!mt_.is_first) {
        mt_.is_first = true;
        mt_.turn_count = 0;
        mt_.multi_turn_rad = 0.0f;
        mt_.multi_turn_lpf = 0.0f;
        mt_.single_turn_last = angle_zero_;
    }

    if (mt_.single_turn_last > JUMP_THRESHOLD && angle_zero_ < (TWO_PI - JUMP_THRESHOLD)) {
        mt_.turn_count++;
    } else if (mt_.single_turn_last < (TWO_PI - JUMP_THRESHOLD) && angle_zero_ > JUMP_THRESHOLD) {
        mt_.turn_count--;
    }

    mt_.single_turn_rad = angle_zero_;
    mt_.multi_turn_rad = mt_.turn_count * TWO_PI + angle_zero_;
    mt_.multi_turn_lpf += 0.2f * (mt_.multi_turn_rad - mt_.multi_turn_lpf);
    mt_.single_turn_last = angle_zero_;
}

float PosController::update(float pos_target_rad) {
    float error = pos_target_rad - mt_.multi_turn_lpf;

    // 反向间隙死区
    if (dz_latched_) {
        if (std::fabs(error) > (backlash_cfg_.deadzone + backlash_cfg_.hysteresis)) {
            dz_latched_ = false;
        }
    } else if (std::fabs(error) < backlash_cfg_.deadzone) {
        dz_latched_ = true;
    }

    float pid_input;
    if (dz_latched_) {
        if (std::fabs(pid_output_) < backlash_cfg_.cmd_threshold) {
            if (dz_cnt_ < dz_cnt_max_) dz_cnt_++;
        } else {
            dz_cnt_ = 0;
        }
    } else {
        dz_cnt_ = 0;
    }

    pid_input = (dz_cnt_ >= dz_cnt_max_) ? 0.0f : error;
    pid_output_ = pid_.update(pid_input);

    return pid_output_;
}

void PosController::set_zero_offset(float offset) {
    zero_offset_ = offset;
    has_zero_calib_ = true;
}

void PosController::set_max_pos_rad(float max_rad) {
    (void)max_rad;
}
