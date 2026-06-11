#include "control/control_app.h"
#include "board/board_devices.h"
#include "control/motor_context.h"
#include "control/controller/speed_controller.h"
#include "control/controller/protection.h"
#include "control/controller/position_sensor.h"
#include "control/controller/pos_controller.h"
#include "control/controller/calibration.h"
#include "comm/can_handler.h"

#include <algo/ntc_sensor.h>
#include <algo/pid_controller.h>
#include <algo/sweep_signal.h>

#include <device.h>
#include <device_base.h>
#include <assert.h>
#include <drivers/flash.h>
#include <drivers_generated.h>
#include <log.h>
#include <osal.h>
#include <sensor_core.h>
#include <irq.h>

#include <nvs/nvs.h>
#include <foc/motor.h>
#include <foc/config.h>
#include <algo/leso.h>

#ifdef CONFIG_LINK
#include <link/router.h>
#include <link/uart_link.h>
#include <link/can_link.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>

// Constants

namespace {

constexpr uint32_t SPEED_LOOP_HZ = 4000;
constexpr size_t SPEED_LOOP_STACK = 2048;
constexpr int32_t SPEED_LOOP_PRIO = 4;

constexpr uint32_t SLOW_LOOP_HZ = CONFIG_FOC_SPEED_LOOP_HZ;
constexpr size_t SLOW_LOOP_STACK = 2048;
constexpr int32_t SLOW_LOOP_PRIO = 6;

constexpr uint32_t POS_LOOP_HZ = 1000;
constexpr size_t POS_LOOP_STACK = 2048;
constexpr int32_t POS_LOOP_PRIO = 5;

constexpr uint32_t LED_HZ = 2;
constexpr size_t LED_STACK = 512;
constexpr int32_t LED_PRIO = 10;

constexpr uint32_t CTRL_LOOP_HZ = 2000;
constexpr size_t CTRL_LOOP_STACK = 2048;
constexpr int32_t CTRL_LOOP_PRIO = 3;

constexpr size_t CLI_BUF_SIZE = 128;

constexpr uint32_t NVS_OFFSET = 512U * 1024U - 3U * 2048U;

constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;

// DM-4340 parameters

constexpr float MOTOR_TORQUE_CONSTANT = 4.074f;
constexpr float MOTOR_GEAR_RATIO = 40.0f;
constexpr uint8_t MOTOR_POLE_PAIRS = 14;

// NTC lookup table

static const NtcPoint NTC_TABLE[] = {
    {-20.0f, 105.3847f}, {-15.0f, 77.8981f}, {-10.0f, 58.2457f},
    {-5.0f,  44.0260f},  {0.0f,   33.6206f},  {5.0f,  25.9246f},
    {10.0f,  20.1746f},  {15.0f,  15.8371f},  {20.0f, 12.5353f},
    {25.0f,  10.000f},   {30.0f,   8.0371f},  {35.0f,  6.5055f},
    {40.0f,   5.3015f},  {45.0f,   4.3481f},  {50.0f,  3.5882f},
    {55.0f,   2.9784f},  {60.0f,   2.4862f},  {65.0f,  2.0864f},
    {70.0f,   1.7598f},  {75.0f,   1.4917f},  {80.0f,  1.2703f},
    {85.0f,   1.0867f},  {90.0f,   0.9336f},  {95.0f,  0.8054f},
    {100.0f,  0.6975f},  {105.0f,  0.6064f},  {110.0f, 0.5291f},
    {115.0f,  0.4633f},  {120.0f,  0.4071f},  {125.0f, 0.3588f},
    {130.0f,  0.3173f},  {135.0f,  0.2814f},  {140.0f, 0.2503f},
    {145.0f,  0.2233f},  {150.0f,  0.1997f},
};

// Motor contexts

static MotorContext g_motors[MAX_MOTORS];
static uint8_t g_motor_count = 0;
static uint8_t g_active_motor = 0;  // Currently selected CLI motor.
static osal::PeriodicThread *g_led_thread = nullptr;

// IMU data updated from SensorCore ISR
#ifdef CONFIG_IMU_ICM40609D
#include <imu/icm40609d.h>
static imu::ImuData g_imu_data;

static bool imu_read_in_isr(void *) {
    return app::board::imu().read(g_imu_data);
}
#endif

// Link communication

#ifdef CONFIG_LINK
static link::UartLink g_uart_link(app::board::console());
static link::CanLink  g_can_link(0, 0x100);

static void comm_init() {
    auto &router = link::Router::instance();

    g_uart_link.set_id(1);
    g_can_link.set_id(2);
    router.set_self_addr(link::make_addr(0x10, 0));

    // Route table: PC (host_id=0x10) to UART, main controller (host_id=0x40) to CAN.
    static const link::RouteEntry routes[] = {
        link::make_route(link::route_by_host(0x10, 0xF0).to(1)),
        link::make_route(link::route_by_host(0x40, 0xF0).to(2)),
        link::make_route(link::route_direct(0).to(1)),
    };
    router.set_routes(routes, 3);

    LOGI("link", "comm initialized: self=0x%02x uart=%d can=%d",
         link::make_addr(0x10, 0), g_uart_link.id(), g_can_link.id());
}
#endif

static char cli_buf[CLI_BUF_SIZE];
static size_t cli_pos = 0;

// NVS

static hal::Flash &nvs_flash() {
    static hal::Flash flash(hal::flash_create_default());
    return flash;
}

static nvs::Nvs<hal::Flash> &nvs_store() {
    static nvs::Nvs<hal::Flash> instance(nvs_flash(), NVS_OFFSET);
    return instance;
}

static bool nvs_load_calib(MotorContext &ctx) {
    CalibData cal{};
    int32_t r = nvs_store().read(ctx.nvs_id_calib(), cal);
    if (r > 0) {
        ctx.motor_cfg.rs = cal.rs;
        ctx.motor_cfg.ld = cal.ld;
        ctx.motor_cfg.lq = cal.lq;
        ctx.motor_cfg.flux_linkage = cal.flux_linkage;
        ctx.motor_cfg.pole_pairs = cal.pole_pairs;
        LOGI("nvs", "loaded calib: Rs=%.4f Ld=%.6f pp=%d", cal.rs, cal.ld, cal.pole_pairs);
        return true;
    }
    return false;
}

static void nvs_save_calib(const MotorContext &ctx) {
    CalibData cal{};
    cal.rs = ctx.motor_cfg.rs;
    cal.ld = ctx.motor_cfg.ld;
    cal.lq = ctx.motor_cfg.lq;
    cal.flux_linkage = ctx.motor_cfg.flux_linkage;
    cal.pole_pairs = ctx.motor_cfg.pole_pairs;
    (void)nvs_store().write(ctx.nvs_id_calib(), cal);
    LOGI("nvs", "calib saved: Rs=%.4f Ld=%.6f", cal.rs, cal.ld);
}

static bool nvs_load_pos_offset(MotorContext &ctx) {
    PosOffsetNvsData pd{};
    int32_t r = nvs_store().read(ctx.nvs_id_pos_offset(), pd);
    if (r > 0 && pd.calib_done) {
        ctx.pos_sensor.set_calib_params(pd.off_v1, pd.off_v2, pd.amp_v1, pd.amp_v2);
        ctx.pos_sensor.set_zero_offset(pd.zero_offset);
        LOGI("nvs", "loaded pos offset: zero=%.3f", pd.zero_offset);
        return true;
    }
    return false;
}

static void nvs_save_pos_offset(MotorContext &ctx, float zero_offset,
                                const TmrCalibData &v1, const TmrCalibData &v2) {
    PosOffsetNvsData pd{};
    pd.zero_offset = zero_offset;
    pd.off_v1 = v1.offset;
    pd.off_v2 = v2.offset;
    pd.amp_v1 = v1.amplitude;
    pd.amp_v2 = v2.amplitude;
    pd.calib_done = true;
    (void)nvs_store().write(ctx.nvs_id_pos_offset(), pd);
    LOGI("nvs", "pos offset saved: zero=%.3f", zero_offset);
}

static bool nvs_load_current_calib(MotorContext &ctx) {
    CurrentCalibNvsData cd{};
    int32_t r = nvs_store().read(ctx.nvs_id_current_calib(), cd);
    if (r > 0 && cd.calib_done) {
        ctx.current_calib = cd;
        LOGI("nvs", "loaded current calib: iu_off=%.4f iv_off=%.4f iw_off=%.4f",
             cd.iu_offset, cd.iv_offset, cd.iw_offset);
        return true;
    }
    ctx.current_calib.iu_k = 1.0f;
    ctx.current_calib.iv_k = 1.0f;
    ctx.current_calib.iw_k = 1.0f;
    return false;
}

static void nvs_save_current_calib(MotorContext &ctx) {
    ctx.current_calib.calib_done = true;
    (void)nvs_store().write(ctx.nvs_id_current_calib(), ctx.current_calib);
    LOGI("nvs", "current calib saved");
}

static bool nvs_load_output_angle(MotorContext &ctx) {
    OutputAngleNvsData od{};
    int32_t r = nvs_store().read(ctx.nvs_id_output_angle(), od);
    if (r > 0 && od.calib_done) {
        memcpy(ctx.output_angle_table, od.table, sizeof(od.table));
        ctx.output_angle_calib_done = true;
        LOGI("nvs", "loaded output angle calib (36 points)");
        return true;
    }
    return false;
}

static bool nvs_load_max_pos(MotorContext &ctx) {
    MaxPosNvsData md{};
    int32_t r = nvs_store().read(ctx.nvs_id_max_pos(), md);
    if (r > 0 && md.calib_done) {
        ctx.max_pos_rad = md.max_pos_rad;
        ctx.max_pos_calib_done = true;
        LOGI("nvs", "loaded max pos: %.3f rad", md.max_pos_rad);
        return true;
    }
    return false;
}

static void nvs_save_max_pos(MotorContext &ctx, float max_rad) {
    MaxPosNvsData md{};
    md.max_pos_rad = max_rad;
    md.calib_done = true;
    (void)nvs_store().write(ctx.nvs_id_max_pos(), md);
    ctx.max_pos_rad = max_rad;
    ctx.max_pos_calib_done = true;
    LOGI("nvs", "max pos saved: %.3f rad", max_rad);
}

// Speed loop thread (4 kHz)

static void speed_loop_entry(void *arg, const osal::PeriodicStats &) {
    auto &ctx = *static_cast<MotorContext *>(arg);
    if (!ctx.motor) return;

    float speed_fb = ctx.motor->speed_rpm();

    switch (ctx.ctrl_mode) {
    case ControlMode::Speed: {
        float iq = ctx.speed_ctrl.update(ctx.speed_setpoint, speed_fb);
        ctx.motor->set_torque(iq);
        break;
    }
    case ControlMode::Sweep: {
        float sweep_out = 0.0f;
        if (!ctx.speed_sweep.update(sweep_out)) {
            float iq = ctx.speed_ctrl.update(ctx.speed_setpoint + sweep_out, speed_fb);
            ctx.motor->set_torque(iq);
        } else {
            LOGI("sweep", "speed sweep done");
            ctx.ctrl_mode = ControlMode::Speed;
        }
        break;
    }
    case ControlMode::Position:
    case ControlMode::Torque:
    case ControlMode::Idle:
    default:
        break;
    }
}

// Position loop thread (1 kHz)

static void pos_loop_entry(void *arg, const osal::PeriodicStats &) {
    auto &ctx = *static_cast<MotorContext *>(arg);
    if (!ctx.motor || ctx.ctrl_mode != ControlMode::Position) return;

    ctx.output_sensor.update();
    ctx.pos_ctrl.update_sensor(ctx.output_sensor.v1_normalized(),
                               ctx.output_sensor.v2_normalized());

    float pos_cmd = ctx.pos_iter_next();
    float speed_cmd = ctx.pos_ctrl.update(pos_cmd);
    speed_cmd *= -MOTOR_GEAR_RATIO;
    speed_cmd = std::clamp(speed_cmd, -3.5f * MOTOR_GEAR_RATIO, 3.5f * MOTOR_GEAR_RATIO);

    float speed_fb = ctx.motor->speed_rpm();
    float iq = ctx.speed_ctrl.update(speed_cmd, speed_fb);
    ctx.motor->set_torque(iq);
}

// Slow loop thread (1 kHz)

static void slow_loop_entry(void *arg, const osal::PeriodicStats &stats) {
    auto &ctx = *static_cast<MotorContext *>(arg);
    if (!ctx.motor) return;

    ctx.motor->slow_loop();

    // Temperature sampling from dedicated NTC channels
    float board_temp = 25.0f;
    float motor_temp = 25.0f;
    {
        auto &adc = app::board::main_adc();
        uint16_t board_raw = 0, motor_raw = 0;
        if (adc.read(static_cast<hal::AdcChannel>(CONFIG_BOARD_TEMP_ADC_CHANNEL), board_raw) == hal::Status::Ok) {
            float v = static_cast<float>(board_raw) * 3.3f / 4096.0f;
            board_temp = ctx.board_ntc.voltage_to_temp(v);
        }
        if (adc.read(static_cast<hal::AdcChannel>(CONFIG_MOTOR_TEMP_ADC_CHANNEL), motor_raw) == hal::Status::Ok) {
            float v = static_cast<float>(motor_raw) * 3.3f / 4096.0f;
            motor_temp = ctx.motor_ntc.voltage_to_temp(v);
        }
        ctx.motor->set_temperature(board_temp, motor_temp);
    }

    // Protection checks
    float vbus = ctx.motor->bus_voltage();
    float iq = ctx.motor->foc_controller().iq();
    ctx.protection.slow_check(board_temp, motor_temp, std::abs(iq));
    ctx.protection.fast_check(vbus);

    if (ctx.protection.has_fault()) {
        if (ctx.ctrl_mode != ControlMode::Idle) {
            LOGW("protect", "fault: %s", ctx.protection.flag_str());
            ctx.motor->emergency_stop();
            ctx.speed_ctrl.disable();
            ctx.ctrl_mode = ControlMode::Idle;
        }
    }

    // Calibration state machine
    if (ctx.calib.is_running()) {
        float iu = ctx.motor->phase_current_u();
        float iv = 0.0f;
        float iw = ctx.motor->phase_current_w();
        float ea_cmd = ctx.calib.open_loop_angle();
        float ea_meas = ctx.motor->foc_controller().pll_angle() *
                        (TWO_PI / 65536.0f);

        bool running = ctx.calib.update(&iu, &iv, &iw, ea_cmd, ea_meas);

        if (ctx.calib.open_loop_vd() > 0.0f) {
            ctx.motor->foc_controller().set_id_ref(ctx.calib.open_loop_vd());
            ctx.motor->foc_controller().set_iq_ref(0.0f);
        }

        if (!running && ctx.calib.is_done()) {
            auto &r = ctx.calib.result();
            LOGI("calib", "=== calibration done ===");

            if (r.iu.done) {
                ctx.current_calib.iu_offset = r.iu.offset;
                LOGI("calib", "  Iu offset: %.4f", r.iu.offset);
            }
            if (r.iv.done) {
                ctx.current_calib.iv_offset = r.iv.offset;
                LOGI("calib", "  Iv offset: %.4f", r.iv.offset);
            }
            if (r.iw.done) {
                ctx.current_calib.iw_offset = r.iw.offset;
                LOGI("calib", "  Iw offset: %.4f", r.iw.offset);
            }
            nvs_save_current_calib(ctx);

            if (ctx.current_calib.calib_done) {
                foc::Motor::CurrentCalib ccal;
                ccal.offset_u = ctx.current_calib.iu_offset;
                ccal.offset_v = ctx.current_calib.iv_offset;
                ccal.offset_w = ctx.current_calib.iw_offset;
                ccal.gain_u = ctx.current_calib.iu_k;
                ccal.gain_v = ctx.current_calib.iv_k;
                ccal.gain_w = ctx.current_calib.iw_k;
                ctx.motor->set_current_calibration(ccal);
            }

            if (r.tmr_input_v1.done && r.tmr_input_v2.done) {
                ctx.pos_sensor.set_calib_params(
                    r.tmr_input_v1.offset, r.tmr_input_v2.offset,
                    r.tmr_input_v1.amplitude, r.tmr_input_v2.amplitude);
                LOGI("calib", "  TMR V1: off=%.3f amp=%.3f",
                     r.tmr_input_v1.offset, r.tmr_input_v1.amplitude);
            }

            if (r.elec_angle.done) {
                ctx.elec_angle_zero_offset = r.elec_angle.zero_offset;
                memcpy(ctx.elec_angle_table, r.elec_angle.table,
                       sizeof(ctx.elec_angle_table));
                ctx.elec_angle_calib_done = true;

                foc::Motor::AngleLutConfig lut_cfg;
                lut_cfg.table = ctx.elec_angle_table;
                lut_cfg.point_count = ELEC_ANGLE_TABLE_SIZE;
                lut_cfg.x_max = TWO_PI * MOTOR_POLE_PAIRS;
                ctx.motor->set_angle_lut(lut_cfg);
                LOGI("calib", "  Elec angle zero: %.3f (%d points, LUT wired)",
                     r.elec_angle.zero_offset, ELEC_ANGLE_TABLE_SIZE);
            }

            if (r.tmr_output_v1.done && r.tmr_output_v2.done) {
                ctx.output_sensor.set_calib_params(
                    r.tmr_output_v1.offset, r.tmr_output_v2.offset,
                    r.tmr_output_v1.amplitude, r.tmr_output_v2.amplitude);
            }

            ctx.pos_sensor.set_zero_offset(ctx.elec_angle_zero_offset);
            nvs_save_pos_offset(ctx, ctx.elec_angle_zero_offset,
                                r.tmr_input_v1, r.tmr_input_v2);

            ctx.motor->disable();
            ctx.ctrl_mode = ControlMode::Idle;
        }
    }

    // Auto-save completed measurement results.
    if (ctx.motor->measurement().is_done()) {
        auto &meas = ctx.motor->measurement();
        ctx.motor_cfg.rs = meas.rs();
        ctx.motor_cfg.ld = meas.ld();
        ctx.motor_cfg.lq = meas.lq();
        ctx.motor_cfg.flux_linkage = meas.flux_linkage();
        nvs_save_calib(ctx);
    }

    // Periodic status log
    if ((stats.sequence % SLOW_LOOP_HZ) == 0) {
        LOGI("foc[%d]", "spd=%.1f iq=%.2f vbus=%.1f t_b=%.1f t_m=%.1f m=%d p=%s",
             (&ctx - g_motors),
             ctx.motor->speed_rpm(), ctx.motor->foc_controller().iq(),
             vbus, board_temp, motor_temp,
             static_cast<int>(ctx.ctrl_mode), ctx.protection.flag_str());
    }

    // CAN status report (10 Hz)
    if ((stats.sequence % (SLOW_LOOP_HZ / 10)) == 0 && ctx.can.is_ready()) {
        ctx.can.report_status(static_cast<uint8_t>(ctx.ctrl_mode),
                              static_cast<uint32_t>(ctx.protection.flags()));
        ctx.can.report_telemetry(ctx.motor->speed_rpm(),
                                 ctx.motor->foc_controller().iq(), vbus);
        ctx.can.report_temperature(board_temp, motor_temp);
        if (ctx.pos_sensor.is_calibrated()) {
            ctx.can.report_position(ctx.pos_sensor.angle_rad(), 0.0f);
        }
    }

    // First power-on auto calibration.
    if (ctx.auto_calib_pending && stats.sequence > 1000) {
        ctx.auto_calib_pending = false;
        LOGI("calib", "auto-calibration starting...");
        ctx.motor->enable();
        ctx.calib.init(ctx.motor->bus_voltage(), ctx.motor_cfg.pole_pairs,
                       MOTOR_GEAR_RATIO, &ctx.pos_sensor, &ctx.output_sensor);
        ctx.calib.start();
    }
}

// Closed-loop task (2 kHz, SensorCore driven)

static void ctrl_loop_entry(void *arg, const osal::PeriodicStats &stats) {
    // TODO: Add attitude estimation, force control, and related control logic.
    // g_imu_data is updated by the SensorCore ISR on each wakeup.
    (void)arg;
    (void)stats;
}

// LED heartbeat

static void led_heartbeat_entry(void *, const osal::PeriodicStats &stats) {
    auto &led = app::board::status_led();
    if (!g_motors[0].motor) return;

    bool any_fault = false;
    bool any_active = false;
    for (uint8_t i = 0; i < g_motor_count; i++) {
        if (g_motors[i].protection.has_fault()) any_fault = true;
        if (g_motors[i].ctrl_mode != ControlMode::Idle) any_active = true;
    }

    if (any_fault) {
        led.on();
    } else if (any_active) {
        led.toggle();
    } else {
        if ((stats.sequence % LED_HZ) == 0) led.toggle();
    }
}

// CAN command callback

static void can_cmd_callback(CanCmd cmd, const uint8_t *data, uint8_t len) {
    MotorContext &ctx = g_motors[g_active_motor];
    switch (cmd) {
    case CanCmd::Enable:
        if (ctx.protection.allow_enable()) {
            ctx.motor->enable();
            ctx.speed_ctrl.enable();
            ctx.ctrl_mode = ControlMode::Speed;
            ctx.speed_setpoint = 0.0f;
        }
        break;
    case CanCmd::Disable:
        ctx.motor->disable();
        ctx.speed_ctrl.disable();
        ctx.ctrl_mode = ControlMode::Idle;
        break;
    case CanCmd::SetSpeed:
        if (len >= 4) {
            float rpm;
            memcpy(&rpm, data, 4);
            ctx.speed_setpoint = rpm;
            ctx.ctrl_mode = ControlMode::Speed;
        }
        break;
    case CanCmd::SetTorque:
        if (len >= 4) {
            float iq;
            memcpy(&iq, data, 4);
            ctx.torque_setpoint = iq;
            ctx.ctrl_mode = ControlMode::Torque;
        }
        break;
    case CanCmd::SetPosition:
        if (len >= 8) {
            float rad, time_ms;
            memcpy(&rad, data, 4);
            memcpy(&time_ms, data + 4, 4);
            ctx.pos_iter_init(ctx.pos_ctrl.multi_turn_angle(), rad, time_ms, POS_LOOP_HZ);
            ctx.ctrl_mode = ControlMode::Position;
        }
        break;
    case CanCmd::GetStatus:
        if (ctx.can.is_ready()) {
            ctx.can.report_status(static_cast<uint8_t>(ctx.ctrl_mode),
                                  static_cast<uint32_t>(ctx.protection.flags()));
        }
        break;
    case CanCmd::Calibrate:
        if (ctx.motor) {
            ctx.motor->enable();
            ctx.calib.init(ctx.motor->bus_voltage(), ctx.motor_cfg.pole_pairs,
                           MOTOR_GEAR_RATIO, &ctx.pos_sensor, &ctx.output_sensor);
            ctx.calib.start();
        }
        break;
    case CanCmd::GetMaxPosRad:
        if (ctx.can.is_ready()) {
            ctx.can.report_max_pos_rad(ctx.max_pos_rad);
        }
        break;
    case CanCmd::GetSensorRaw:
        if (ctx.can.is_ready() && ctx.motor) {
            ctx.can.report_sensor_raw(
                ctx.motor->phase_current_u(), 0.0f, ctx.motor->phase_current_w(),
                ctx.motor->bus_voltage(), ctx.pos_sensor.angle_rad());
        }
        break;
    case CanCmd::CalibPosZero:
        if (ctx.pos_ctrl.multi_turn_angle() != 0.0f) {
            ctx.pos_ctrl.set_zero_offset(ctx.pos_ctrl.single_turn_angle());
            nvs_save_max_pos(ctx, ctx.max_pos_rad);
            LOGI("can", "pos zero calib: %.3f rad", ctx.pos_ctrl.single_turn_angle());
        }
        break;
    case CanCmd::CalibMaxPos:
        ctx.max_pos_rad = ctx.pos_ctrl.multi_turn_angle();
        ctx.pos_ctrl.set_max_pos_rad(ctx.max_pos_rad);
        nvs_save_max_pos(ctx, ctx.max_pos_rad);
        LOGI("can", "max pos calib: %.3f rad", ctx.max_pos_rad);
        break;
    case CanCmd::Estop:
        ctx.motor->emergency_stop();
        ctx.speed_ctrl.disable();
        ctx.ctrl_mode = ControlMode::Idle;
        break;
    }
}

// CLI

static void cli_process(const char *cmd) {
    MotorContext &ctx = g_motors[g_active_motor];

    if (strncmp(cmd, "motor ", 6) == 0) {
        uint8_t idx = static_cast<uint8_t>(cmd[6] - '0');
        if (idx < g_motor_count) {
            g_active_motor = idx;
            LOGI("cli", "switched to motor %d", idx);
        } else {
            LOGW("cli", "invalid motor index (0-%d)", g_motor_count - 1);
        }
    } else if (strcmp(cmd, "start") == 0 || strcmp(cmd, "en") == 0) {
        if (ctx.motor && ctx.protection.allow_enable()) {
            ctx.motor->enable();
            ctx.speed_ctrl.enable();
            ctx.ctrl_mode = ControlMode::Speed;
            ctx.speed_setpoint = 0.0f;
            LOGI("cli", "motor %d enabled (speed mode)", g_active_motor);
        } else if (!ctx.protection.allow_enable()) {
            LOGW("cli", "blocked by protection: %s", ctx.protection.flag_str());
        }
    } else if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "dis") == 0) {
        if (ctx.motor) {
            ctx.motor->disable();
            ctx.speed_ctrl.disable();
            ctx.ctrl_mode = ControlMode::Idle;
            LOGI("cli", "motor %d disabled", g_active_motor);
        }
    } else if (strncmp(cmd, "speed ", 6) == 0) {
        float rpm = 0;
        if (sscanf(cmd + 6, "%f", &rpm) == 1 && ctx.motor && std::isfinite(rpm)) {
            ctx.speed_setpoint = rpm;
            ctx.ctrl_mode = ControlMode::Speed;
            LOGI("cli", "speed set to %.1f rpm", rpm);
        } else {
            LOGW("cli", "invalid speed");
        }
    } else if (strncmp(cmd, "torque ", 7) == 0) {
        float iq = 0;
        if (sscanf(cmd + 7, "%f", &iq) == 1 && ctx.motor && std::isfinite(iq)) {
            ctx.torque_setpoint = iq;
            ctx.ctrl_mode = ControlMode::Torque;
            LOGI("cli", "torque set to %.2f A", iq);
        } else {
            LOGW("cli", "invalid torque");
        }
    } else if (strncmp(cmd, "pos ", 4) == 0) {
        float rad = 0, time_ms = 0;
        int n = sscanf(cmd + 4, "%f %f", &rad, &time_ms);
        if (n >= 1 && ctx.motor && std::isfinite(rad)) {
            if (n < 2 || time_ms < 1.0f) time_ms = 1000.0f;
            ctx.pos_iter_init(ctx.pos_ctrl.multi_turn_angle(), rad, time_ms, POS_LOOP_HZ);
            ctx.ctrl_mode = ControlMode::Position;
            LOGI("cli", "position set to %.3f rad over %.0f ms", rad, time_ms);
        } else {
            LOGW("cli", "usage: pos <rad> [time_ms]");
        }
    } else if (strncmp(cmd, "sweep_speed ", 12) == 0) {
        float fstart = 0.1f, fend = 20.0f, mag = 5.0f;
        sscanf(cmd + 12, "%f %f %f", &fstart, &fend, &mag);
        SweepConfig scfg;
        scfg.fs = static_cast<float>(SPEED_LOOP_HZ);
        scfg.fstart = fstart;
        scfg.fend = fend;
        scfg.fgap = 0.1f;
        scfg.magnitude = mag;
        scfg.repeat_time = 10;
        ctx.speed_sweep.init(scfg);
        ctx.ctrl_mode = ControlMode::Sweep;
        LOGI("cli", "speed sweep: %.1f-%.1f Hz, mag=%.1f", fstart, fend, mag);
    } else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "st") == 0) {
        if (ctx.motor) {
            auto &foc = ctx.motor->foc_controller();
            LOGI("cli", "=== Motor %d Status ===", g_active_motor);
            LOGI("cli", "  Mode:    %d", static_cast<int>(ctx.ctrl_mode));
            LOGI("cli", "  Speed:   %.1f rpm", ctx.motor->speed_rpm());
            LOGI("cli", "  Id:      %.3f A", foc.id());
            LOGI("cli", "  Iq:      %.3f A", foc.iq());
            LOGI("cli", "  Vd:      %.3f V", foc.vd());
            LOGI("cli", "  Vq:      %.3f V", foc.vq());
            LOGI("cli", "  Vbus:    %.2f V", ctx.motor->bus_voltage());
            LOGI("cli", "  Temp:    %.1f C", ctx.motor->temperature());
            LOGI("cli", "  Errors:  0x%08lx",
                 static_cast<unsigned long>(ctx.motor->errors().error_flags()));
            LOGI("cli", "  Protect: %s", ctx.protection.flag_str());
            LOGI("cli", "  Rs=%.4f Ld=%.6f Lq=%.6f pp=%d",
                 ctx.motor_cfg.rs, ctx.motor_cfg.ld, ctx.motor_cfg.lq, ctx.motor_cfg.pole_pairs);
            if (ctx.speed_ctrl.is_enabled()) {
                LOGI("cli", "  LESO dist: %.3f", ctx.speed_ctrl.disturbance());
                LOGI("cli", "  Speed Iq:  %.3f A", ctx.speed_ctrl.iq_output());
            }
            ctx.pos_sensor.update();
            LOGI("cli", "  Pos angle: %.3f rad", ctx.pos_sensor.angle_rad());
            LOGI("cli", "  Pos calib: %s", ctx.pos_sensor.is_calibrated() ? "yes" : "no");
            LOGI("cli", "  Multi-turn: %.3f rad (%d turns)",
                 ctx.pos_ctrl.multi_turn_angle(), ctx.pos_ctrl.turn_count());
            LOGI("cli", "  Max pos: %.3f rad %s",
                 ctx.max_pos_rad, ctx.max_pos_calib_done ? "(calib)" : "(default)");
            LOGI("cli", "  Cur calib: %s", ctx.current_calib.calib_done ? "yes" : "no");
            LOGI("cli", "  Elec calib: %s", ctx.elec_angle_calib_done ? "yes" : "no");
        }
    } else if (strcmp(cmd, "measure") == 0 || strcmp(cmd, "meas") == 0) {
        if (ctx.motor) {
            ctx.motor->start_measurement();
            LOGI("cli", "motor param measurement started");
        }
    } else if (strcmp(cmd, "calibrate") == 0 || strcmp(cmd, "cal") == 0) {
        if (ctx.motor) {
            ctx.motor->enable();
            ctx.calib.init(ctx.motor->bus_voltage(), ctx.motor_cfg.pole_pairs,
                           MOTOR_GEAR_RATIO, &ctx.pos_sensor, &ctx.output_sensor);
            ctx.calib.start();
            LOGI("cli", "4-stage calibration started: %s", ctx.calib.stage_str());
        }
    } else if (strcmp(cmd, "sensor") == 0 || strcmp(cmd, "sns") == 0) {
        ctx.pos_sensor.update();
        LOGI("cli", "=== Input TMR Sensor ===");
        LOGI("cli", "  Angle:   %.3f rad (%.1f deg)",
             ctx.pos_sensor.angle_rad(),
             ctx.pos_sensor.angle_rad() * 180.0f / static_cast<float>(M_PI));
        LOGI("cli", "  V1 norm: %.3f", ctx.pos_sensor.v1_normalized());
        LOGI("cli", "  V2 norm: %.3f", ctx.pos_sensor.v2_normalized());
        LOGI("cli", "  Calib:   %s", ctx.pos_sensor.is_calibrated() ? "yes" : "no");
        ctx.output_sensor.update();
        LOGI("cli", "=== Output TMR Sensor ===");
        LOGI("cli", "  Angle:   %.3f rad (%.1f deg)",
             ctx.output_sensor.angle_rad(),
             ctx.output_sensor.angle_rad() * 180.0f / static_cast<float>(M_PI));
        LOGI("cli", "  V1 norm: %.3f", ctx.output_sensor.v1_normalized());
        LOGI("cli", "  V2 norm: %.3f", ctx.output_sensor.v2_normalized());
        LOGI("cli", "  Multi-turn: %.3f rad (%d turns)",
             ctx.pos_ctrl.multi_turn_angle(), ctx.pos_ctrl.turn_count());
        if (ctx.calib.is_running()) {
            LOGI("cli", "  Calib stage: %s", ctx.calib.stage_str());
        }
    } else if (strcmp(cmd, "save") == 0) {
        nvs_save_calib(ctx);
    } else if (strcmp(cmd, "reset") == 0) {
        (void)nvs_store().remove(ctx.nvs_id_calib());
        (void)nvs_store().remove(ctx.nvs_id_pos_offset());
        (void)nvs_store().remove(ctx.nvs_id_current_calib());
        (void)nvs_store().remove(ctx.nvs_id_output_angle());
        LOGI("cli", "all calib cleared, reboot to apply defaults");
    } else if (strcmp(cmd, "protect") == 0 || strcmp(cmd, "prot") == 0) {
        LOGI("cli", "=== Protection ===");
        LOGI("cli", "  Flags:   %s", ctx.protection.flag_str());
        LOGI("cli", "  Allow:   %s", ctx.protection.allow_enable() ? "yes" : "NO");
    } else if (strcmp(cmd, "estop") == 0) {
        if (ctx.motor) {
            ctx.motor->emergency_stop();
            ctx.speed_ctrl.disable();
            ctx.ctrl_mode = ControlMode::Idle;
            LOGI("cli", "EMERGENCY STOP");
        }
    } else if (strcmp(cmd, "devices") == 0 || strcmp(cmd, "dev") == 0) {
        size_t count = 0;
        const auto *registry = hal::get_device_registry(&count);
        LOGI("cli", "--- Device Registry (%zu devices) ---", count);
        for (size_t i = 0; i < count; i++) {
            LOGI("cli", "  [%zu] %s (ord=%d, type=%s)",
                 i, registry[i].alias, registry[i].ord, registry[i].type_name);
        }
    } else if (strcmp(cmd, "uart_stats") == 0 || strcmp(cmd, "ust") == 0) {
        auto &uart = app::board::console();
        auto stats = uart.get_stats();
        LOGI("cli", "=== UART Stats ===");
        LOGI("cli", "  TX:       %lu bytes", stats.tx_bytes);
        LOGI("cli", "  RX:       %lu bytes", stats.rx_bytes);
        LOGI("cli", "  Overrun:  %lu", stats.overrun_count);
        LOGI("cli", "  Framing:  %lu", stats.framing_errors);
        LOGI("cli", "  Parity:   %lu", stats.parity_errors);
        LOGI("cli", "  State:    %s", uart.is_initialized() ? "ready" : "not init");
    } else if (strcmp(cmd, "uart_stats reset") == 0 || strcmp(cmd, "ust_r") == 0) {
        app::board::console().reset_stats();
        LOGI("cli", "UART stats reset");
    } else if (strcmp(cmd, "assert_test") == 0) {
        LOGI("cli", "triggering HAL_ASSERT(false)...");
        HAL_ASSERT(false);
    } else if (strcmp(cmd, "help") == 0) {
        LOGI("cli", "=== FOC CLI Commands ===");
        LOGI("cli", "  motor <idx>         - Select active motor (0-%d)", g_motor_count - 1);
        LOGI("cli", "  start / en          - Enable motor (speed mode)");
        LOGI("cli", "  stop / dis          - Disable motor");
        LOGI("cli", "  speed <rpm>         - Set speed (RPM)");
        LOGI("cli", "  torque <A>          - Set torque current");
        LOGI("cli", "  pos <rad> [ms]      - Set position");
        LOGI("cli", "  sweep_speed [f0 f1 m] - Speed sweep test");
        LOGI("cli", "  status / st         - Show motor status");
        LOGI("cli", "  sensor / sns        - Show position sensors");
        LOGI("cli", "  measure / meas      - Auto-measure motor params");
        LOGI("cli", "  calibrate / cal     - Run 4-stage calibration");
        LOGI("cli", "  save                - Save calibration to NVS");
        LOGI("cli", "  reset               - Clear all NVS calibration");
        LOGI("cli", "  protect / prot      - Show protection status");
        LOGI("cli", "  estop               - Emergency stop");
        LOGI("cli", "  --- HAL Diagnostics ---");
        LOGI("cli", "  devices / dev       - List registered devices");
        LOGI("cli", "  uart_stats / ust    - Show UART stats");
        LOGI("cli", "  uart_stats reset    - Reset UART stats");
        LOGI("cli", "  assert_test         - Trigger HAL_ASSERT(false)");
        LOGI("cli", "  help                - Show this help");
    } else {
        LOGW("cli", "unknown: '%s', type 'help'", cmd);
    }
}

} // namespace

// Single motor initialization

static int init_motor(MotorContext &ctx, uint8_t motor_idx) {
    char spd_name[16], pos_name[16], slw_name[16];
    snprintf(spd_name, sizeof(spd_name), "spd_%d", motor_idx);
    snprintf(pos_name, sizeof(pos_name), "pos_%d", motor_idx);
    snprintf(slw_name, sizeof(slw_name), "slow_%d", motor_idx);

    // NVS load
    if (nvs_store().mount() == nvs::Status::Ok) {
        nvs_load_calib(ctx);
        nvs_load_pos_offset(ctx);
        nvs_load_current_calib(ctx);
        nvs_load_output_angle(ctx);
        nvs_load_max_pos(ctx);

        if (ctx.current_calib.calib_done) {
            foc::Motor::CurrentCalib ccal;
            ccal.offset_u = ctx.current_calib.iu_offset;
            ccal.offset_v = ctx.current_calib.iv_offset;
            ccal.offset_w = ctx.current_calib.iw_offset;
            ccal.gain_u = ctx.current_calib.iu_k;
            ccal.gain_v = ctx.current_calib.iv_k;
            ccal.gain_w = ctx.current_calib.iw_k;
            ctx.motor->set_current_calibration(ccal);
        }

        if (ctx.elec_angle_calib_done) {
            foc::Motor::AngleLutConfig lut_cfg;
            lut_cfg.table = ctx.elec_angle_table;
            lut_cfg.point_count = ELEC_ANGLE_TABLE_SIZE;
            lut_cfg.x_max = TWO_PI * MOTOR_POLE_PAIRS;
            ctx.motor->set_angle_lut(lut_cfg);
        }

        if (ctx.motor_cfg.rs > 0.001f) {
            ctx.motor->foc_controller().calculate_gains(
                ctx.motor_cfg.rs, ctx.motor_cfg.ld, ctx.motor_cfg.lq,
                ctx.motor_cfg.flux_linkage, ctx.motor_cfg.pole_pairs);
        }
    } else {
        LOGW("nvs", "mount failed, using DTS defaults");
    }

    // Input TMR sensor
    TmrSensorConfig tmr_in_cfg;
    tmr_in_cfg.ch_v1 = 8;
    tmr_in_cfg.ch_v2 = 9;
    tmr_in_cfg.v_ref = 3.3f;
    ctx.pos_sensor.init(tmr_in_cfg);

    // Output TMR sensor
    TmrSensorConfig tmr_out_cfg;
    tmr_out_cfg.ch_v1 = 5;
    tmr_out_cfg.ch_v2 = 6;
    tmr_out_cfg.v_ref = 3.3f;
    ctx.output_sensor.init(tmr_out_cfg);

    // NTC temperature sensors
    NtcConfig ntc_cfg;
    ntc_cfg.divider_resistance = 10000.0f;
    ntc_cfg.v_ref = 3.3f;
    ctx.board_ntc.init(ntc_cfg, NTC_TABLE, sizeof(NTC_TABLE) / sizeof(NTC_TABLE[0]));
    ctx.motor_ntc.init(ntc_cfg, NTC_TABLE, sizeof(NTC_TABLE) / sizeof(NTC_TABLE[0]));

    // Position controller
    PidConfig pos_pid_cfg;
    pos_pid_cfg.kp = 6.0f;
    pos_pid_cfg.ki = 0.0f;
    pos_pid_cfg.kd = 0.0f;
    pos_pid_cfg.ts = 1.0f / POS_LOOP_HZ;
    pos_pid_cfg.i_limit = 3.5f;
    pos_pid_cfg.output_limit = 3.5f;
    BacklashConfig backlash_cfg;
    backlash_cfg.deadzone = 0.006f;
    backlash_cfg.hysteresis = 0.01f;
    backlash_cfg.cmd_threshold = 1.0f;
    backlash_cfg.dwell_time_s = 1.0f;
    float *out_angle_tbl = ctx.output_angle_calib_done ? ctx.output_angle_table : nullptr;
    ctx.pos_ctrl.init(pos_pid_cfg, backlash_cfg,
                      out_angle_tbl, OUTPUT_ANGLE_TABLE_SIZE,
                      static_cast<float>(POS_LOOP_HZ));
    if (ctx.max_pos_calib_done) {
        ctx.pos_ctrl.set_max_pos_rad(ctx.max_pos_rad);
    }

    // Protection
    ProtectionConfig prot_cfg;
    prot_cfg.bus_overvoltage = 45.0f;
    prot_cfg.bus_undervoltage = 12.0f;
    prot_cfg.bus_ov_recovery = 42.0f;
    prot_cfg.bus_uv_recovery = 15.0f;
    prot_cfg.board_overtemp = 100.0f;
    prot_cfg.motor_overtemp = 120.0f;
    prot_cfg.board_ot_recovery = 50.0f;
    prot_cfg.motor_ot_recovery = 80.0f;
    prot_cfg.overcurrent = CONFIG_FOC_MAX_CURRENT_A * 1.2f;
    prot_cfg.fast_trigger_cnt = 20;
    prot_cfg.fast_recover_cnt = 100;
    prot_cfg.slow_trigger_cnt = 2000;
    prot_cfg.slow_recover_cnt = 2000;
    ctx.protection.init(prot_cfg);

    // Speed loop LESO
    SpeedController::Config spd_cfg;
    spd_cfg.leso.type = algo::LesoType::FirstOrder;
    spd_cfg.leso.omega = 3000.0f;
    spd_cfg.leso.b0 = 6000.0f;
    spd_cfg.leso.ts = 1.0f / SPEED_LOOP_HZ;
    spd_cfg.leso.kp = 0.006f;
    spd_cfg.leso.output_min = -CONFIG_FOC_MAX_CURRENT_A;
    spd_cfg.leso.output_max = CONFIG_FOC_MAX_CURRENT_A;
    spd_cfg.torque_constant = MOTOR_TORQUE_CONSTANT;
    spd_cfg.max_current = CONFIG_FOC_MAX_CURRENT_A;
    ctx.speed_ctrl.init(spd_cfg);

    // CAN
    ctx.can.init(ctx.can_base_id);
    ctx.can.set_callback(can_cmd_callback);

    // Speed loop thread, driven by hardware timer TIMER5 (4 kHz).
    auto &speed_tim = app::board::speed_timer();
    ctx.speed_loop = osal::PeriodicThread::create(spd_name,
        speed_loop_entry, &ctx,
        SPEED_LOOP_STACK, SPEED_LOOP_PRIO,
        SPEED_LOOP_HZ, osal::PeriodicTrigger::External, &speed_tim);
    if (!ctx.speed_loop || ctx.speed_loop->startup() != 0) {
        LOGE("foc", "failed to start speed loop for motor %d", motor_idx);
        return -1;
    }

    // Position loop thread
    ctx.pos_loop = osal::PeriodicThread::create(pos_name,
        pos_loop_entry, &ctx,
        POS_LOOP_STACK, POS_LOOP_PRIO,
        POS_LOOP_HZ, osal::PeriodicTrigger::Tick);
    if (!ctx.pos_loop || ctx.pos_loop->startup() != 0) {
        LOGE("foc", "failed to start pos loop for motor %d", motor_idx);
        return -1;
    }

    // Slow loop thread
    ctx.slow_loop = osal::PeriodicThread::create(slw_name,
        slow_loop_entry, &ctx,
        SLOW_LOOP_STACK, SLOW_LOOP_PRIO,
        SLOW_LOOP_HZ, osal::PeriodicTrigger::Tick);
    if (!ctx.slow_loop || ctx.slow_loop->startup() != 0) {
        LOGE("foc", "failed to start slow loop for motor %d", motor_idx);
        return -1;
    }

    // Closed-loop SensorCore task
    // IMU device initialization is handled by initcall
    {
        auto &imu_tim = app::board::control_timer();
        SensorCore::Config sc_cfg;
        sc_cfg.name = "ctrl";
        sc_cfg.entry = ctrl_loop_entry;
        sc_cfg.param = &ctx;
        sc_cfg.stack_size = CTRL_LOOP_STACK;
        sc_cfg.priority = CTRL_LOOP_PRIO;
        sc_cfg.frequency_hz = CTRL_LOOP_HZ;
        sc_cfg.timer = &imu_tim;
// IMU data updated from SensorCore ISR
#ifdef CONFIG_IMU_ICM40609D
        sc_cfg.read_fn = imu_read_in_isr;
        sc_cfg.divider = 8;
#endif
        ctx.imu_core = new SensorCore(sc_cfg);
        if (ctx.imu_core->start() != 0) {
            LOGE("foc", "failed to start imu core for motor %d", motor_idx);
            return -1;
        }
    }

    // First power-on auto calibration.
    if (!ctx.current_calib.calib_done || !ctx.pos_sensor.is_calibrated()) {
        ctx.auto_calib_pending = true;
        LOGI("foc", "motor %d: no calib data, auto-calib will run after 1s", motor_idx);
    }

    return 0;
}

// Public API

namespace app {

int start_control() {
    (void)nvs_flash().init();

    // Initialize motor 0.
    auto &motor_dev = app::board::main_motor();
    auto &ctx0 = g_motors[0];
    ctx0.motor = &motor_dev.motor();
    ctx0.motor_cfg.rs = CONFIG_FOC_RS_OHMS * 0.001f;
    ctx0.motor_cfg.ld = CONFIG_FOC_LD_HENRIES * 1e-6f;
    ctx0.motor_cfg.lq = CONFIG_FOC_LQ_HENRIES * 1e-6f;
    ctx0.motor_cfg.flux_linkage = CONFIG_FOC_FLUX_LINKAGE * 1e-3f;
    ctx0.motor_cfg.pole_pairs = static_cast<uint8_t>(CONFIG_FOC_POLE_PAIRS);
    ctx0.can_base_id = 0x100;
    ctx0.nvs_id_base = 0x0001;
    g_motor_count = 1;

    if (init_motor(ctx0, 0) != 0) return -1;

#ifdef CONFIG_LINK
    comm_init();
#endif

    // TODO: Initialize motor 1 here if a second motor is needed.
    // auto &motor_dev1 = app::board::secondary_motor();
    // auto &ctx1 = g_motors[1];
    // ctx1.motor = &motor_dev1.motor();
    // ctx1.motor_cfg = ...;
    // ctx1.can_base_id = 0x200;
    // ctx1.nvs_id_base = 0x0101;
    // g_motor_count = 2;
    // if (init_motor(ctx1, 1) != 0) return -1;

    // LED heartbeat
    g_led_thread = osal::PeriodicThread::create("led_hb",
        led_heartbeat_entry, nullptr,
        LED_STACK, LED_PRIO,
        LED_HZ, osal::PeriodicTrigger::Tick);
    if (!g_led_thread || g_led_thread->startup() != 0) {
        LOGE("foc", "failed to start LED thread");
    }

    LOGI("foc", "FOC system initialized (%d motor(s), %dpp, gear=%d, LESO+PID pos ctrl)",
         g_motor_count, MOTOR_POLE_PAIRS, static_cast<int>(MOTOR_GEAR_RATIO));
    return 0;
}

void feed_control_char(char c) {
    if (c == '\n' || c == '\r') {
        if (cli_pos > 0) {
            cli_buf[cli_pos] = '\0';
            cli_process(cli_buf);
            cli_pos = 0;
        }
    } else if (cli_pos < CLI_BUF_SIZE - 1) {
        cli_buf[cli_pos++] = c;
    }
}

} // namespace app
