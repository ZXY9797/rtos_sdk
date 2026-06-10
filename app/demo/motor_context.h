#pragma once

#include "controller/speed_controller.h"
#include "controller/protection.h"
#include "controller/position_sensor.h"
#include "controller/pos_controller.h"
#include "controller/calibration.h"
#include "comm/can_handler.h"

#include <algo/ntc_sensor.h>
#include <algo/sweep_signal.h>
#include <foc/motor.h>
#include <osal.h>

#include <cstdint>
#include <cstring>

// ─── 控制模式 ─────────────────────────────────────────────────

enum class ControlMode : uint8_t {
    Idle = 0,
    Torque,
    Speed,
    Position,
    Sweep,
};

// ─── NVS 数据结构 ─────────────────────────────────────────────

struct CalibData {
    float rs;
    float ld;
    float lq;
    float flux_linkage;
    uint8_t pole_pairs;
    uint8_t reserved[3];
};
static_assert(sizeof(CalibData) == 20);

struct ProtectNvsData {
    float bus_ov;
    float bus_uv;
    float board_ot;
    float motor_ot;
};
static_assert(sizeof(ProtectNvsData) == 16);

struct LesoNvsData {
    float omega;
    float b0;
    float kp;
};
static_assert(sizeof(LesoNvsData) == 12);

struct PosOffsetNvsData {
    float zero_offset;
    float off_v1;
    float off_v2;
    float amp_v1;
    float amp_v2;
    bool calib_done;
    uint8_t reserved[3];
};
static_assert(sizeof(PosOffsetNvsData) == 24);

struct CurrentCalibNvsData {
    float iu_offset;
    float iv_offset;
    float iw_offset;
    float iu_k;
    float iv_k;
    float iw_k;
    bool calib_done;
    uint8_t reserved[3];
};
static_assert(sizeof(CurrentCalibNvsData) == 28);

struct OutputAngleNvsData {
    float table[36];
    bool calib_done;
    uint8_t reserved[3];
};
static_assert(sizeof(OutputAngleNvsData) == 148);

struct MaxPosNvsData {
    float max_pos_rad;
    bool calib_done;
    uint8_t reserved[3];
};
static_assert(sizeof(MaxPosNvsData) == 8);

// ─── 电机上下文 ───────────────────────────────────────────────

struct MotorContext {
    // 硬件
    foc::Motor *motor {nullptr};
    foc::MotorConfig motor_cfg;

    // 控制器
    SpeedController speed_ctrl;
    Protection protection;
    PositionSensor pos_sensor;
    PositionSensor output_sensor;
    PosController pos_ctrl;
    Calibration calib;
    NtcSensor board_ntc;
    NtcSensor motor_ntc;
    SweepSignal speed_sweep;
    SweepSignal pos_sweep;

    // CAN
    CanHandler can;
    uint32_t can_base_id {0x100};

    // 状态
    ControlMode ctrl_mode {ControlMode::Idle};
    float speed_setpoint {0.0f};
    float torque_setpoint {0.0f};
    float pos_setpoint {0.0f};
    float pos_current {0.0f};
    uint32_t pos_iter_max {0};
    uint32_t pos_iter_cnt {0};
    float pos_step {0.0f};

    // 校准数据
    CurrentCalibNvsData current_calib {};
    float elec_angle_table[36] {};
    bool elec_angle_calib_done {false};
    float elec_angle_zero_offset {0.0f};
    float output_angle_table[36] {};
    bool output_angle_calib_done {false};
    float max_pos_rad {6.283185307f};
    bool max_pos_calib_done {false};
    bool auto_calib_pending {false};

    // 线程句柄
    osal::PeriodicThread *speed_loop {nullptr};
    osal::PeriodicThread *slow_loop {nullptr};
    osal::PeriodicThread *pos_loop {nullptr};

    // NVS ID 偏移基址 (motor0=0x0001, motor1=0x0101)
    uint16_t nvs_id_base {0x0001};

    // NVS ID 计算
    uint16_t nvs_id_calib()         const { return nvs_id_base + 0; }
    uint16_t nvs_id_protect()       const { return nvs_id_base + 1; }
    uint16_t nvs_id_leso()          const { return nvs_id_base + 2; }
    uint16_t nvs_id_pos_offset()    const { return nvs_id_base + 3; }
    uint16_t nvs_id_current_calib() const { return nvs_id_base + 4; }
    uint16_t nvs_id_output_angle()  const { return nvs_id_base + 5; }
    uint16_t nvs_id_max_pos()       const { return nvs_id_base + 6; }

    // 位置插补
    void pos_iter_init(float current, float target, float time_ms, uint32_t pos_loop_hz) {
        pos_current = current;
        pos_setpoint = target;
        if (time_ms < 99.0f) {
            pos_current = target;
            pos_iter_max = 0;
            pos_step = 0.0f;
        } else {
            pos_iter_max = static_cast<uint32_t>(time_ms * pos_loop_hz / 1000.0f);
            pos_step = (target - current) / static_cast<float>(pos_iter_max);
        }
        pos_iter_cnt = 0;
    }

    float pos_iter_next() {
        if (pos_iter_cnt < pos_iter_max) {
            pos_current += pos_step;
            pos_iter_cnt++;
        } else {
            pos_current = pos_setpoint;
        }
        return pos_current;
    }
};

// ─── 多电机实例 ───────────────────────────────────────────────

static constexpr uint8_t MAX_MOTORS = 2;

// 电机上下文数组（由 foc_app::start 填充）
// 实际定义在 foc_app.cc 中
