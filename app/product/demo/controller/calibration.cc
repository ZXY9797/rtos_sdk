#include "calibration.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

void Calibration::init(float vbus, uint8_t pole_pairs, float gear_ratio,
                       PositionSensor *input_tmr, PositionSensor *output_tmr) {
    vbus_ = vbus;
    pole_pairs_ = pole_pairs;
    gear_ratio_ = gear_ratio;
    input_tmr_ = input_tmr;
    output_tmr_ = output_tmr;
    stage_ = CalibStage::Idle;
    state_ = CalibState::Stop;
}

void Calibration::start() {
    stage_ = CalibStage::Idle;
    state_ = CalibState::Stop;
    start_next_stage();
}

const char *Calibration::stage_str() const {
    switch (stage_) {
    case CalibStage::Idle:            return "idle";
    case CalibStage::CurrentZeroIu:   return "cur_zero_iu";
    case CalibStage::CurrentZeroIv:   return "cur_zero_iv";
    case CalibStage::CurrentZeroIw:   return "cur_zero_iw";
    case CalibStage::TmrInput:        return "tmr_input";
    case CalibStage::ElecAngle:       return "elec_angle";
    case CalibStage::TmrOutput:       return "tmr_output";
    case CalibStage::Done:            return "done";
    }
    return "?";
}

void Calibration::start_next_stage() {
    // 按顺序查找下一个未完成的阶段
    if (!result_.iu.done) {
        stage_ = CalibStage::CurrentZeroIu;
    } else if (!result_.iv.done) {
        stage_ = CalibStage::CurrentZeroIv;
    } else if (!result_.iw.done) {
        stage_ = CalibStage::CurrentZeroIw;
    } else if (!result_.tmr_input_v1.done || !result_.tmr_input_v2.done) {
        stage_ = CalibStage::TmrInput;
    } else if (!result_.elec_angle.done) {
        stage_ = CalibStage::ElecAngle;
    } else if (!result_.tmr_output_v1.done || !result_.tmr_output_v2.done) {
        stage_ = CalibStage::TmrOutput;
    } else {
        stage_ = CalibStage::Done;
        state_ = CalibState::Stop;
        return;
    }

    // 初始化阶段
    state_ = CalibState::Running;
    sample_cnt_ = 0;
    step_cnt_ = 0;
    step_timer_ = 0;
    open_loop_angle_ = 0.0f;

    switch (stage_) {
    case CalibStage::CurrentZeroIu:
    case CalibStage::CurrentZeroIv:
    case CalibStage::CurrentZeroIw:
        open_loop_vd_ = 0.0f;  // 不施加电压
        sum_iu_ = sum_iv_ = sum_iw_ = 0.0f;
        break;

    case CalibStage::TmrInput:
        open_loop_vd_ = CALIB_VD_VOLTAGE;
        tmr_v1_min_ = 1e9f; tmr_v1_max_ = -1e9f;
        tmr_v2_min_ = 1e9f; tmr_v2_max_ = -1e9f;
        tmr_v1_sum_ = tmr_v2_sum_ = 0.0f;
        break;

    case CalibStage::ElecAngle:
        open_loop_vd_ = CALIB_VD_ELEC;
        break;

    case CalibStage::TmrOutput:
        open_loop_vd_ = CALIB_VD_VOLTAGE;
        tmr_v1_min_ = 1e9f; tmr_v1_max_ = -1e9f;
        tmr_v2_min_ = 1e9f; tmr_v2_max_ = -1e9f;
        break;

    default:
        break;
    }
}

bool Calibration::update(float *phase_u, float *phase_v, float *phase_w,
                          float elec_angle_cmd, float elec_angle_measured) {
    if (state_ != CalibState::Running) {
        return false;
    }

    switch (stage_) {
    case CalibStage::CurrentZeroIu:
        run_current_zero(result_.iu, phase_u ? *phase_u : 0.0f);
        break;
    case CalibStage::CurrentZeroIv:
        run_current_zero(result_.iv, phase_v ? *phase_v : 0.0f);
        break;
    case CalibStage::CurrentZeroIw:
        run_current_zero(result_.iw, phase_w ? *phase_w : 0.0f);
        break;

    case CalibStage::TmrInput:
        if (input_tmr_) {
            run_tmr_input(input_tmr_->raw_voltage_v1(),
                          input_tmr_->raw_voltage_v2());
        }
        break;

    case CalibStage::ElecAngle:
        run_elec_angle(elec_angle_cmd, elec_angle_measured);
        break;

    case CalibStage::TmrOutput:
        if (output_tmr_) {
            run_tmr_output(output_tmr_->raw_voltage_v1(),
                           output_tmr_->raw_voltage_v2());
        }
        break;

    case CalibStage::Done:
    case CalibStage::Idle:
        return false;
    }

    return true;
}

// ─── Stage 1: 电流零偏 ──────────────────────────────────────────

void Calibration::run_current_zero(CurrentCalibData &cal, float current) {
    sum_iu_ += current;
    sample_cnt_++;

    if (sample_cnt_ >= CURRENT_ZERO_SAMPLES) {
        cal.offset = sum_iu_ / static_cast<float>(sample_cnt_);
        cal.done = true;
        state_ = CalibState::Finish;
        start_next_stage();
    }
}

// ─── Stage 2: 输入 TMR 传感器 ────────────────────────────────────

void Calibration::run_tmr_input(float v1, float v2) {
    step_timer_ += MS_PER_TICK;

    if (step_timer_ >= STEP_INTERVAL_MS) {
        step_timer_ = 0;

        // 记录 min/max
        tmr_v1_min_ = std::min(tmr_v1_min_, v1);
        tmr_v1_max_ = std::max(tmr_v1_max_, v1);
        tmr_v2_min_ = std::min(tmr_v2_min_, v2);
        tmr_v2_max_ = std::max(tmr_v2_max_, v2);
        tmr_v1_sum_ += v1;
        tmr_v2_sum_ += v2;

        sample_cnt_++;
        open_loop_angle_ += CALIB_ANGLE_STEP;

        if (sample_cnt_ >= TMR_INPUT_SAMPLES) {
            // 计算 offset 和 amplitude
            float n = static_cast<float>(sample_cnt_);
            result_.tmr_input_v1.offset = tmr_v1_sum_ / n;
            result_.tmr_input_v2.offset = tmr_v2_sum_ / n;
            result_.tmr_input_v1.amplitude = (tmr_v1_max_ - tmr_v1_min_) * 0.5f;
            result_.tmr_input_v2.amplitude = (tmr_v2_max_ - tmr_v2_min_) * 0.5f;
            result_.tmr_input_v1.done = true;
            result_.tmr_input_v2.done = true;
            state_ = CalibState::Finish;
            open_loop_vd_ = 0.0f;
            start_next_stage();
        }
    }
}

// ─── Stage 3: 电角度校准 ────────────────────────────────────────

void Calibration::run_elec_angle(float cmd, float measured) {
    step_timer_ += MS_PER_TICK;

    if (step_timer_ >= STEP_INTERVAL_MS) {
        step_timer_ = 0;

        if (sample_cnt_ == 0) {
            elec_angle_cmd_first_ = cmd;
            elec_angle_measured_first_ = measured;
            result_.elec_angle.zero_offset = cmd - measured;
        }

        // 记录命令角度与测量角度的偏差
        float unwrapped = measured - elec_angle_measured_first_;
        if (unwrapped < 0.0f) unwrapped += 2.0f * static_cast<float>(M_PI) * pole_pairs_;
        float cmd_unwrapped = cmd - elec_angle_cmd_first_;
        if (cmd_unwrapped < 0.0f) cmd_unwrapped += 2.0f * static_cast<float>(M_PI) * pole_pairs_;

        result_.elec_angle.table[sample_cnt_] = cmd_unwrapped - unwrapped;

        sample_cnt_++;
        open_loop_angle_ += CALIB_ANGLE_STEP;

        if (sample_cnt_ >= ELEC_ANGLE_SAMPLES) {
            result_.elec_angle.done = true;
            state_ = CalibState::Finish;
            open_loop_vd_ = 0.0f;
            start_next_stage();
        }
    }
}

// ─── Stage 4: 输出 TMR 传感器 ────────────────────────────────────

void Calibration::run_tmr_output(float v1, float v2) {
    step_timer_ += MS_PER_TICK;

    if (step_timer_ >= STEP_INTERVAL_MS) {
        step_timer_ = 0;

        tmr_v1_min_ = std::min(tmr_v1_min_, v1);
        tmr_v1_max_ = std::max(tmr_v1_max_, v1);
        tmr_v2_min_ = std::min(tmr_v2_min_, v2);
        tmr_v2_max_ = std::max(tmr_v2_max_, v2);

        sample_cnt_++;
        open_loop_angle_ += CALIB_ANGLE_STEP;

        if (sample_cnt_ >= TMR_OUTPUT_SAMPLES) {
            result_.tmr_output_v1.offset = (tmr_v1_max_ + tmr_v1_min_) * 0.5f;
            result_.tmr_output_v2.offset = (tmr_v2_max_ + tmr_v2_min_) * 0.5f;
            result_.tmr_output_v1.amplitude = (tmr_v1_max_ - tmr_v1_min_) * 0.5f;
            result_.tmr_output_v2.amplitude = (tmr_v2_max_ - tmr_v2_min_) * 0.5f;
            result_.tmr_output_v1.done = true;
            result_.tmr_output_v2.done = true;
            state_ = CalibState::Finish;
            open_loop_vd_ = 0.0f;
            start_next_stage();
        }
    }
}
