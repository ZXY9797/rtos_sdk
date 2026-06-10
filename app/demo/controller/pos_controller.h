#pragma once

#include <cstdint>
#include <algo/lagrange_interp.h>
#include <algo/pid_controller.h>

// 多圈角度信息
struct MultiTurnAngle {
    bool is_first {false};
    int32_t turn_count {0};
    float single_turn_rad {0.0f};
    float multi_turn_rad {0.0f};
    float multi_turn_lpf {0.0f};
    float single_turn_last {0.0f};
};

// 反向间隙死区配置
struct BacklashConfig {
    float deadzone {0.006f};
    float hysteresis {0.01f};
    float cmd_threshold {1.0f};
    float dwell_time_s {1.0f};
};

class PosController {
public:
    void init(const PidConfig &pid_cfg, const BacklashConfig &backlash_cfg,
              float *output_angleOffsetTable, uint32_t offset_table_size,
              float loop_hz);

    // 更新输出 TMR 传感器并计算多圈角度
    void update_sensor(float v1_norm, float v2_norm);

    // 位置闭环，返回速度指令 (rad/s)
    float update(float pos_target_rad);

    void set_zero_offset(float offset);
    void set_max_pos_rad(float max_rad);

    float multi_turn_angle() const { return mt_.multi_turn_lpf; }
    float single_turn_angle() const { return mt_.single_turn_rad; }
    int32_t turn_count() const { return mt_.turn_count; }
    float output_angle_raw() const { return angle_raw_; }

    bool has_output_calib() const { return has_output_calib_; }
    bool has_zero_calib() const { return has_zero_calib_; }

private:
    PidController pid_;
    BacklashConfig backlash_cfg_;
    LagrangeInterp output_interp_;
    bool has_output_calib_ {false};
    bool has_zero_calib_ {false};
    float zero_offset_ {0.0f};

    MultiTurnAngle mt_;
    float angle_raw_ {0.0f};
    float angle_calib_ {0.0f};
    float angle_zero_ {0.0f};

    // 反向间隙状态
    bool dz_latched_ {false};
    uint32_t dz_cnt_ {0};
    uint32_t dz_cnt_max_ {0};
    float pid_output_ {0.0f};

    static constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;
    static constexpr float JUMP_THRESHOLD = TWO_PI * 0.9f;
};
