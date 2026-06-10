#pragma once

#include "position_sensor.h"
#include <cstdint>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

// 4 阶段校准流程（参照 DM_4340_canopen）
// 1. 电流零偏校准 (Iu, Iv, Iw)
// 2. 输入 TMR 传感器校准 (offset + amplitude)
// 3. 电角度校准 (TMR 电角度 vs 命令角度查表)
// 4. 输出 TMR 传感器校准 (减速后输出轴)

enum class CalibStage : uint8_t {
    Idle = 0,
    CurrentZeroIu,
    CurrentZeroIv,
    CurrentZeroIw,
    TmrInput,
    ElecAngle,
    TmrOutput,
    Done,
};

enum class CalibState : uint8_t {
    Stop = 0,
    Running,
    Finish,
};

struct CurrentCalibData {
    bool done {false};
    float offset {0.0f};
};

struct TmrCalibData {
    bool done {false};
    float offset {0.0f};
    float amplitude {0.0f};
};

static constexpr uint32_t ELEC_ANGLE_TABLE_SIZE = 112;
static constexpr uint32_t OUTPUT_ANGLE_TABLE_SIZE = 36;

struct ElecAngleCalibData {
    bool done {false};
    float zero_offset {0.0f};
    float table[ELEC_ANGLE_TABLE_SIZE] {};
};

struct OutputAngleCalibData {
    bool done {false};
    float table[OUTPUT_ANGLE_TABLE_SIZE] {};
};

struct CalibResult {
    CurrentCalibData iu, iv, iw;
    TmrCalibData tmr_input_v1, tmr_input_v2;
    ElecAngleCalibData elec_angle;
    TmrCalibData tmr_output_v1, tmr_output_v2;
    OutputAngleCalibData output_angle;
};

class Calibration {
public:
    void init(float vbus, uint8_t pole_pairs, float gear_ratio,
              PositionSensor *input_tmr, PositionSensor *output_tmr);

    // 慢循环中调用，返回 true 表示校准进行中
    bool update(float *phase_current_u, float *phase_current_v, float *phase_current_w,
                float elec_angle_cmd, float elec_angle_measured);

    // 启动校准
    void start();

    // 查询状态
    CalibStage stage() const { return stage_; }
    CalibState state() const { return state_; }
    const char *stage_str() const;
    bool is_done() const { return stage_ == CalibStage::Done; }
    bool is_running() const { return state_ == CalibState::Running; }

    // 获取结果
    const CalibResult &result() const { return result_; }

    // 设置开环电压（校准用）
    float open_loop_vd() const { return open_loop_vd_; }
    float open_loop_angle() const { return open_loop_angle_; }

private:
    // 配置
    float vbus_ {24.0f};
    uint8_t pole_pairs_ {14};
    float gear_ratio_ {40.0f};
    PositionSensor *input_tmr_ {nullptr};
    PositionSensor *output_tmr_ {nullptr};

    // 状态
    CalibStage stage_ {CalibStage::Idle};
    CalibState state_ {CalibState::Stop};
    uint32_t sample_cnt_ {0};
    uint32_t step_cnt_ {0};
    uint32_t step_timer_ {0};

    // 开环控制
    float open_loop_vd_ {0.0f};
    float open_loop_angle_ {0.0f};

    // 采集缓存
    float sum_iu_ {0.0f}, sum_iv_ {0.0f}, sum_iw_ {0.0f};
    float tmr_v1_min_ {0.0f}, tmr_v1_max_ {0.0f};
    float tmr_v2_min_ {0.0f}, tmr_v2_max_ {0.0f};
    float tmr_v1_sum_ {0.0f}, tmr_v2_sum_ {0.0f};

    // 电角度校准
    float elec_angle_cmd_first_ {0.0f};
    float elec_angle_measured_first_ {0.0f};

    // 结果
    CalibResult result_;

    // 内部方法
    void start_next_stage();
    void run_current_zero(CurrentCalibData &cal, float current);
    void run_tmr_input(float v1, float v2);
    void run_elec_angle(float cmd, float measured);
    void run_tmr_output(float v1, float v2);

    static constexpr uint32_t CURRENT_ZERO_SAMPLES = 100;
    static constexpr uint32_t TMR_INPUT_SAMPLES = 112;    // 2 圈机械角度
    static constexpr uint32_t ELEC_ANGLE_SAMPLES = 112;
    static constexpr uint32_t TMR_OUTPUT_SAMPLES = 8960;  // 112 * 40 * 2
    static constexpr float CALIB_VD_VOLTAGE = 2.0f;
    static constexpr float CALIB_VD_ELEC = 3.0f;
    static constexpr float CALIB_ANGLE_STEP = static_cast<float>(M_PI) / 4.0f;
    static constexpr uint32_t STEP_INTERVAL_MS = 100;
    static constexpr uint32_t MS_PER_TICK = 1;  // 1kHz 慢循环
};
