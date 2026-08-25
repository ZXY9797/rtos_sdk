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
#include <boot_layout.h>
#include <log.h>
#include <init.h>
#include <osal.h>
#include <sensor_core.h>
#include <system/watchdog.h>
#include <irq.h>

#include <nvs/nvs.h>
#include <foc/motor.h>
#include <foc/config.h>
#include <algo/leso.h>

#ifdef CONFIG_LINK
#include <link/router.h>
#include <link/uart_link.h>
#endif

#include <algorithm>
#include <cstddef>
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

constexpr float TWO_PI = 2.0f * 3.14159265358979323846f;

#if defined(CONFIG_APP_WATCHDOG)
system_watchdog::ClientId g_control_watchdog =
    system_watchdog::kInvalidClient;

int register_control_watchdog()
{
    g_control_watchdog = system_watchdog::register_client(
        "control", CONFIG_APP_WATCHDOG_TIMEOUT_MS);
    return g_control_watchdog != system_watchdog::kInvalidClient ? 0 : -1;
}

SYS_INIT(register_control_watchdog, INITCALL_LEVEL_APPLICATION, 90);
#endif

// DM-4340 parameters

constexpr float MOTOR_TORQUE_CONSTANT =
    static_cast<float>(CONFIG_MOTOR_TORQUE_CONSTANT) * 0.001F;
constexpr float MOTOR_GEAR_RATIO =
    static_cast<float>(CONFIG_MOTOR_GEAR_RATIO);

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
static std::atomic<uint8_t> g_active_motor {0U};
struct MotorRuntimeStorage {
    osal::PeriodicThread speed_thread;
    osal::PeriodicThread position_thread;
    osal::PeriodicThread slow_thread;
    SensorCore sensor_core;
    alignas(std::max_align_t) uint8_t speed_stack[SPEED_LOOP_STACK] {};
    alignas(std::max_align_t) uint8_t position_stack[POS_LOOP_STACK] {};
    alignas(std::max_align_t) uint8_t slow_stack[SLOW_LOOP_STACK] {};
    alignas(std::max_align_t) uint8_t sensor_stack[CTRL_LOOP_STACK] {};
};
static MotorRuntimeStorage g_motor_runtime[MAX_MOTORS];
static osal::PeriodicThread g_led_thread_storage;
alignas(std::max_align_t) static uint8_t g_led_stack[LED_STACK] {};
static osal::PeriodicThread *g_led_thread = nullptr;

// IMU data updated by the SensorCore worker thread.
#ifdef CONFIG_IMU_ICM40609D
#include <imu/icm40609d.h>
static imu::ImuData g_imu_data;

static bool imu_read_sample(void *) {
    return app::board::imu().read(g_imu_data);
}
#endif

// Link communication

#ifdef CONFIG_LINK
static link::UartLink g_uart_link(app::board::console());

static bool comm_init() {
    auto &router = link::Router::instance();

    g_uart_link.set_id(1);
    router.set_self_addr(link::make_addr(0x1, 0));

    // CAN0 is exclusively owned by the legacy motor-command dispatcher. Mixing
    // two independent consumers on the same hardware RX FIFO loses frames.
    static const link::RouteEntry routes[] = {
        link::make_route(link::route_by_host(0x10, 0xF0).to(1)),
        link::make_route(link::route_direct(0).to(1)),
    };
    if (!router.set_routes(routes, 2)) {
        LOGE("link", "invalid route table");
        return false;
    }

    LOGI("link", "comm initialized: self=0x%02x uart=%d",
         link::make_addr(0x1, 0), g_uart_link.id());
    return true;
}

static void comm_deinit() {
    auto &router = link::Router::instance();
    (void)router.set_routes(nullptr, 0);
    router.set_self_addr(0);
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
    hal::Flash &flash = nvs_flash();
    const uint32_t nvs_span = flash.erase_sector_size()
        * static_cast<uint32_t>(CONFIG_NVS_SECTOR_COUNT);
    HAL_ASSERT(nvs_span <= boot::layout::kStorageSize);
    // Keep boot-control records at the beginning of the storage partition.
    static nvs::Nvs<hal::Flash> instance(
        flash,
        boot::layout::kStorageOffset + boot::layout::kStorageSize - nvs_span);
    return instance;
}

static bool finite_range(float value, float minimum, float maximum) {
    return std::isfinite(value) && value >= minimum && value <= maximum;
}

static bool valid_calib(const CalibData &value) {
    return finite_range(value.rs, 0.000001F, 100.0F)
        && finite_range(value.ld, 0.000000001F, 1.0F)
        && finite_range(value.lq, 0.000000001F, 1.0F)
        && finite_range(value.flux_linkage, 0.0F, 10.0F)
        && value.pole_pairs >= 1U && value.pole_pairs <= 64U;
}

static bool valid_position_offset(const PosOffsetNvsData &value) {
    return finite_range(value.zero_offset, -100.0F, 100.0F)
        && finite_range(value.off_v1, -10.0F, 10.0F)
        && finite_range(value.off_v2, -10.0F, 10.0F)
        && finite_range(value.amp_v1, 0.001F, 10.0F)
        && finite_range(value.amp_v2, 0.001F, 10.0F);
}

static bool valid_current_calib(const CurrentCalibNvsData &value) {
    return finite_range(value.iu_offset, -100.0F, 100.0F)
        && finite_range(value.iv_offset, -100.0F, 100.0F)
        && finite_range(value.iw_offset, -100.0F, 100.0F)
        && finite_range(value.iu_k, 0.001F, 100.0F)
        && finite_range(value.iv_k, 0.001F, 100.0F)
        && finite_range(value.iw_k, 0.001F, 100.0F);
}

static bool valid_output_angle(const OutputAngleNvsData &value) {
    for (float point : value.table) {
        if (!finite_range(point, -TWO_PI, TWO_PI)) return false;
    }
    return true;
}

static bool valid_maximum_position(float value) {
    return finite_range(value, 0.001F, 10000.0F);
}

static bool nvs_load_calib(MotorContext &ctx) {
    CalibData cal{};
    int32_t r = nvs_store().read(ctx.nvs_id_calib(), cal);
    if (r == static_cast<int32_t>(sizeof(cal)) && valid_calib(cal)) {
        ctx.motor_cfg.rs = cal.rs;
        ctx.motor_cfg.ld = cal.ld;
        ctx.motor_cfg.lq = cal.lq;
        ctx.motor_cfg.flux_linkage = cal.flux_linkage;
        ctx.motor_cfg.pole_pairs = cal.pole_pairs;
        LOGI("nvs", "loaded calib: Rs=%.4f Ld=%.6f pp=%d", cal.rs, cal.ld, cal.pole_pairs);
        return true;
    }
    if (r >= 0) LOGW("nvs", "invalid motor calibration record");
    return false;
}

static bool nvs_save_calib(const MotorContext &ctx) {
    CalibData cal{};
    cal.rs = ctx.motor_cfg.rs;
    cal.ld = ctx.motor_cfg.ld;
    cal.lq = ctx.motor_cfg.lq;
    cal.flux_linkage = ctx.motor_cfg.flux_linkage;
    cal.pole_pairs = ctx.motor_cfg.pole_pairs;
    if (!valid_calib(cal)) {
        LOGE("nvs", "refusing invalid motor calibration");
        return false;
    }
    const int32_t result = nvs_store().write(ctx.nvs_id_calib(), cal);
    if (result < 0) {
        LOGE("nvs", "calib save failed: %ld", static_cast<long>(result));
        return false;
    }
    LOGI("nvs", "calib saved: Rs=%.4f Ld=%.6f", cal.rs, cal.ld);
    return true;
}

static bool nvs_load_pos_offset(MotorContext &ctx) {
    PosOffsetNvsData pd{};
    int32_t r = nvs_store().read(ctx.nvs_id_pos_offset(), pd);
    if (r == static_cast<int32_t>(sizeof(pd)) && pd.calib_done
        && valid_position_offset(pd)) {
        ctx.pos_sensor.set_calib_params(pd.off_v1, pd.off_v2, pd.amp_v1, pd.amp_v2);
        ctx.pos_sensor.set_zero_offset(pd.zero_offset);
        LOGI("nvs", "loaded pos offset: zero=%.3f", pd.zero_offset);
        return true;
    }
    if (r >= 0) LOGW("nvs", "invalid position-offset record");
    return false;
}

static bool nvs_save_pos_offset(MotorContext &ctx, float zero_offset,
                                const TmrCalibData &v1, const TmrCalibData &v2) {
    PosOffsetNvsData pd{};
    pd.zero_offset = zero_offset;
    pd.off_v1 = v1.offset;
    pd.off_v2 = v2.offset;
    pd.amp_v1 = v1.amplitude;
    pd.amp_v2 = v2.amplitude;
    pd.calib_done = true;
    if (!valid_position_offset(pd)) {
        LOGE("nvs", "refusing invalid position-offset calibration");
        return false;
    }
    const int32_t result = nvs_store().write(ctx.nvs_id_pos_offset(), pd);
    if (result < 0) {
        LOGE("nvs", "position-offset save failed: %ld",
             static_cast<long>(result));
        return false;
    }
    LOGI("nvs", "pos offset saved: zero=%.3f", zero_offset);
    return true;
}

static bool nvs_load_current_calib(MotorContext &ctx) {
    CurrentCalibNvsData cd{};
    int32_t r = nvs_store().read(ctx.nvs_id_current_calib(), cd);
    if (r == static_cast<int32_t>(sizeof(cd)) && cd.calib_done
        && valid_current_calib(cd)) {
        ctx.current_calib = cd;
        LOGI("nvs", "loaded current calib: iu_off=%.4f iv_off=%.4f iw_off=%.4f",
             cd.iu_offset, cd.iv_offset, cd.iw_offset);
        return true;
    }
    if (r >= 0) LOGW("nvs", "invalid current calibration record");
    ctx.current_calib.iu_k = 1.0f;
    ctx.current_calib.iv_k = 1.0f;
    ctx.current_calib.iw_k = 1.0f;
    return false;
}

static bool nvs_save_current_calib(MotorContext &ctx) {
    CurrentCalibNvsData pending = ctx.current_calib;
    pending.calib_done = true;
    if (!valid_current_calib(pending)) {
        LOGE("nvs", "refusing invalid current calibration");
        return false;
    }
    const int32_t result = nvs_store().write(ctx.nvs_id_current_calib(), pending);
    if (result < 0) {
        LOGE("nvs", "current calibration save failed: %ld",
             static_cast<long>(result));
        return false;
    }
    ctx.current_calib = pending;
    LOGI("nvs", "current calib saved");
    return true;
}

static bool nvs_load_output_angle(MotorContext &ctx) {
    OutputAngleNvsData od{};
    int32_t r = nvs_store().read(ctx.nvs_id_output_angle(), od);
    if (r == static_cast<int32_t>(sizeof(od)) && od.calib_done
        && valid_output_angle(od)) {
        memcpy(ctx.output_angle_table, od.table, sizeof(od.table));
        ctx.output_angle_calib_done = true;
        LOGI("nvs", "loaded output angle calib (36 points)");
        return true;
    }
    if (r >= 0) LOGW("nvs", "invalid output-angle record");
    return false;
}

static bool nvs_load_max_pos(MotorContext &ctx) {
    MaxPosNvsData md{};
    int32_t r = nvs_store().read(ctx.nvs_id_max_pos(), md);
    if (r == static_cast<int32_t>(sizeof(md)) && md.calib_done
        && valid_maximum_position(md.max_pos_rad)) {
        ctx.max_pos_rad = md.max_pos_rad;
        ctx.max_pos_calib_done = true;
        LOGI("nvs", "loaded max pos: %.3f rad", md.max_pos_rad);
        return true;
    }
    if (r >= 0) LOGW("nvs", "invalid maximum-position record");
    return false;
}

static bool nvs_save_max_pos(MotorContext &ctx, float max_rad) {
    MaxPosNvsData md{};
    md.max_pos_rad = max_rad;
    md.calib_done = true;
    if (!valid_maximum_position(max_rad)) {
        LOGE("nvs", "refusing invalid maximum position");
        return false;
    }
    const int32_t result = nvs_store().write(ctx.nvs_id_max_pos(), md);
    if (result < 0) {
        LOGE("nvs", "maximum-position save failed: %ld",
             static_cast<long>(result));
        return false;
    }
    ctx.max_pos_rad = max_rad;
    ctx.max_pos_calib_done = true;
    LOGI("nvs", "max pos saved: %.3f rad", max_rad);
    return true;
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
        osal::LockGuard sweep_lock(ctx.sweep_mutex);
        if (!sweep_lock.owns_lock()) {
            ctx.motor->set_torque(0.0F);
            break;
        }
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
        break;
    case ControlMode::Torque:
        ctx.motor->set_torque(ctx.torque_setpoint.load(std::memory_order_acquire));
        break;
    case ControlMode::Idle:
    default:
        break;
    }
}

// Position loop thread (1 kHz)

static void pos_loop_entry(void *arg, const osal::PeriodicStats &) {
    auto &ctx = *static_cast<MotorContext *>(arg);
    if (!ctx.motor || ctx.ctrl_mode != ControlMode::Position) return;
    osal::LockGuard position_lock(ctx.position_mutex);
    if (!position_lock.owns_lock()
        || ctx.ctrl_mode.load(std::memory_order_acquire)
               != ControlMode::Position) {
        return;
    }

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
#if defined(CONFIG_APP_WATCHDOG)
    if (&ctx == &g_motors[0]
        && !system_watchdog::heartbeat(g_control_watchdog)) {
        return;
    }
#endif
    if (!ctx.motor) return;

    ctx.motor->slow_loop();

    // A single task owns CAN RX. Bound work per cycle so a saturated bus
    // cannot starve protection and calibration processing.
    for (uint8_t frame = 0U; frame < 8U && ctx.can.process(); ++frame) {
    }

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
            ctx.motor->set_current_reference(ctx.calib.open_loop_vd(), 0.0F);
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
            const bool current_calib_saved = nvs_save_current_calib(ctx);

            if (current_calib_saved) {
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
                lut_cfg.x_max = TWO_PI * ctx.motor_cfg.pole_pairs;
                if (!ctx.motor->set_angle_lut(lut_cfg)) {
                    LOGE("calib", "invalid electrical-angle LUT");
                    ctx.motor->emergency_stop();
                }
                LOGI("calib", "  Elec angle zero: %.3f (%d points, LUT wired)",
                     r.elec_angle.zero_offset, ELEC_ANGLE_TABLE_SIZE);
            }

            if (r.tmr_output_v1.done && r.tmr_output_v2.done) {
                ctx.output_sensor.set_calib_params(
                    r.tmr_output_v1.offset, r.tmr_output_v2.offset,
                    r.tmr_output_v1.amplitude, r.tmr_output_v2.amplitude);
            }

            ctx.pos_sensor.set_zero_offset(ctx.elec_angle_zero_offset);
            (void)nvs_save_pos_offset(ctx, ctx.elec_angle_zero_offset,
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
        (void)nvs_save_calib(ctx);
    }

    // Periodic status log
    if ((stats.sequence % SLOW_LOOP_HZ) == 0) {
        LOGI("foc[%d]", "spd=%.1f iq=%.2f vbus=%.1f t_b=%.1f t_m=%.1f m=%d p=%s",
             (&ctx - g_motors),
             ctx.motor->speed_rpm(), ctx.motor->foc_controller().iq(),
             vbus, board_temp, motor_temp,
             static_cast<int>(ctx.ctrl_mode.load(std::memory_order_acquire)),
             ctx.protection.flag_str());
    }

    // CAN status report (10 Hz)
    if ((stats.sequence % (SLOW_LOOP_HZ / 10)) == 0 && ctx.can.is_ready()) {
        ctx.can.report_status(static_cast<uint8_t>(
                                  ctx.ctrl_mode.load(std::memory_order_acquire)),
                              static_cast<uint32_t>(ctx.protection.flags()));
        ctx.can.report_telemetry(ctx.motor->speed_rpm(),
                                 ctx.motor->foc_controller().iq(), vbus);
        ctx.can.report_temperature(board_temp, motor_temp);
        osal::LockGuard position_lock(ctx.position_mutex);
        if (position_lock.owns_lock() && ctx.pos_sensor.is_calibrated()) {
            ctx.can.report_position(ctx.pos_sensor.angle_rad(), 0.0f);
        }
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
    MotorContext &ctx = g_motors[
        g_active_motor.load(std::memory_order_acquire)];
    osal::LockGuard position_lock(ctx.position_mutex);
    if (!position_lock.owns_lock()) {
        return;
    }
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
        if (len >= sizeof(float)) {
            float rad = 0.0F;
            float time_ms = 0.0F;
            memcpy(&rad, data, 4);
            if (len >= 2U * sizeof(float)) {
                memcpy(&time_ms, data + sizeof(float), sizeof(float));
            }
            ctx.pos_iter_init(ctx.pos_ctrl.multi_turn_angle(), rad, time_ms, POS_LOOP_HZ);
            ctx.ctrl_mode = ControlMode::Position;
        }
        break;
    case CanCmd::GetStatus:
        if (ctx.can.is_ready()) {
            ctx.can.report_status(static_cast<uint8_t>(
                                      ctx.ctrl_mode.load(std::memory_order_acquire)),
                                  static_cast<uint32_t>(ctx.protection.flags()));
        }
        break;
    case CanCmd::Calibrate:
        // The existing estimator has no synchronized board-level open-loop
        // voltage/angle execution path. Never energize from this command.
        LOGW("can", "calibration unavailable");
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
            (void)nvs_save_max_pos(ctx, ctx.max_pos_rad);
            LOGI("can", "pos zero calib: %.3f rad", ctx.pos_ctrl.single_turn_angle());
        }
        break;
    case CanCmd::CalibMaxPos:
        ctx.max_pos_rad = ctx.pos_ctrl.multi_turn_angle();
        ctx.pos_ctrl.set_max_pos_rad(ctx.max_pos_rad);
        (void)nvs_save_max_pos(ctx, ctx.max_pos_rad);
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
    const uint8_t active = g_active_motor.load(std::memory_order_acquire);
    MotorContext &ctx = g_motors[active];
    osal::LockGuard position_lock(ctx.position_mutex);
    if (!position_lock.owns_lock()) {
        LOGW("cli", "control state busy");
        return;
    }

    if (strncmp(cmd, "motor ", 6) == 0) {
        uint8_t idx = static_cast<uint8_t>(cmd[6] - '0');
        if (idx < g_motor_count) {
            g_active_motor.store(idx, std::memory_order_release);
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
            LOGI("cli", "motor %d enabled (speed mode)", active);
        } else if (!ctx.protection.allow_enable()) {
            LOGW("cli", "blocked by protection: %s", ctx.protection.flag_str());
        }
    } else if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "dis") == 0) {
        if (ctx.motor) {
            ctx.motor->disable();
            ctx.speed_ctrl.disable();
            ctx.ctrl_mode = ControlMode::Idle;
            LOGI("cli", "motor %d disabled", active);
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
        float fstart = static_cast<float>(CONFIG_SWEEP_FSTART) * 0.1F;
        float fend = static_cast<float>(CONFIG_SWEEP_FEND) * 0.1F;
        float mag = 5.0F;
        sscanf(cmd + 12, "%f %f %f", &fstart, &fend, &mag);
        SweepConfig scfg;
        scfg.fs = static_cast<float>(SPEED_LOOP_HZ);
        scfg.fstart = fstart;
        scfg.fend = fend;
        scfg.fgap = static_cast<float>(CONFIG_SWEEP_FGAP) * 0.1F;
        scfg.magnitude = mag;
        scfg.repeat_time = CONFIG_SWEEP_REPEAT;
        osal::LockGuard sweep_lock(ctx.sweep_mutex);
        if (sweep_lock.owns_lock()) {
            ctx.speed_sweep.init(scfg);
            ctx.ctrl_mode = ControlMode::Sweep;
            LOGI("cli", "speed sweep: %.1f-%.1f Hz, mag=%.1f", fstart, fend, mag);
        } else {
            LOGW("cli", "speed sweep configuration busy");
        }
    } else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "st") == 0) {
        if (ctx.motor) {
            auto &foc = ctx.motor->foc_controller();
            LOGI("cli", "=== Motor %d Status ===", active);
            LOGI("cli", "  Mode:    %d", static_cast<int>(
                     ctx.ctrl_mode.load(std::memory_order_acquire)));
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
            const hal::Status result = ctx.motor->start_measurement();
            if (result == hal::Status::Ok) {
                LOGI("cli", "motor param measurement started");
            } else {
                LOGW("cli", "motor param measurement unavailable (%d)",
                     static_cast<int>(result));
            }
        }
    } else if (strcmp(cmd, "calibrate") == 0 || strcmp(cmd, "cal") == 0) {
        LOGW("cli", "calibration unavailable: board excitation path missing");
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
        (void)nvs_save_calib(ctx);
    } else if (strcmp(cmd, "reset") == 0) {
        const nvs::Status calib_status =
            nvs_store().remove(ctx.nvs_id_calib());
        const nvs::Status position_status =
            nvs_store().remove(ctx.nvs_id_pos_offset());
        const nvs::Status current_status =
            nvs_store().remove(ctx.nvs_id_current_calib());
        const nvs::Status output_status =
            nvs_store().remove(ctx.nvs_id_output_angle());
        const nvs::Status maximum_status =
            nvs_store().remove(ctx.nvs_id_max_pos());
        const bool removed = calib_status == nvs::Status::Ok
            && position_status == nvs::Status::Ok
            && current_status == nvs::Status::Ok
            && output_status == nvs::Status::Ok
            && maximum_status == nvs::Status::Ok;
        if (removed) {
            LOGI("cli", "all calib cleared, reboot to apply defaults");
        } else {
            LOGE("cli", "failed to clear one or more calibration records");
        }
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
            const bool ready = registry[i].is_ready(registry[i].instance);
            LOGI("cli", "  [%zu] %s (ord=%d, type=%s, ready=%s)",
                 i, registry[i].alias, registry[i].ord,
                 registry[i].type_name, ready ? "yes" : "no");
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
        LOGI("cli", "  measure / meas      - Motor params (unavailable)");
        LOGI("cli", "  calibrate / cal     - Calibration (unavailable)");
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

static void destroy_periodic_thread(osal::PeriodicThread *&thread)
{
    if (thread != nullptr) {
        thread->destroy();
        thread = nullptr;
    }
}

static void rollback_motor_runtime(MotorContext &ctx)
{
    if (ctx.motor != nullptr) {
        ctx.motor->disable();
    }
    if (ctx.imu_core != nullptr) {
        HAL_ASSERT(ctx.imu_core->stop() == 0);
        ctx.imu_core = nullptr;
    }
    destroy_periodic_thread(ctx.slow_loop);
    destroy_periodic_thread(ctx.pos_loop);
    destroy_periodic_thread(ctx.speed_loop);
    ctx.can.deinit();
}

static bool start_periodic_thread(osal::PeriodicThread *&thread,
                                  osal::PeriodicThread &storage,
                                  const osal::PeriodicThreadConfig& config)
{
    if (thread != nullptr || !storage.start(config)) {
        return false;
    }
    if (storage.startup() == 0) {
        thread = &storage;
        return true;
    }
    storage.destroy();
    return false;
}

static int init_motor(MotorContext &ctx, uint8_t motor_idx) {
    if (motor_idx >= MAX_MOTORS) {
        return -1;
    }
    MotorRuntimeStorage &runtime = g_motor_runtime[motor_idx];
    char spd_name[16], pos_name[16], slw_name[16];
    snprintf(spd_name, sizeof(spd_name), "spd_%d", motor_idx);
    snprintf(pos_name, sizeof(pos_name), "pos_%d", motor_idx);
    snprintf(slw_name, sizeof(slw_name), "slow_%d", motor_idx);

    // NVS load
    if (nvs_store().mount() == nvs::Status::Ok) {
        const bool motor_calibration_loaded = nvs_load_calib(ctx);
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
            lut_cfg.x_max = TWO_PI * ctx.motor_cfg.pole_pairs;
            if (!ctx.motor->set_angle_lut(lut_cfg)) {
                LOGE("foc", "invalid stored electrical-angle LUT");
                rollback_motor_runtime(ctx);
                return -1;
            }
        }

        if (motor_calibration_loaded) {
            ctx.motor->configure_current_loop(
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
    ntc_cfg.divider_resistance =
        static_cast<float>(CONFIG_NTC_DIVIDER_RESISTANCE);
    ntc_cfg.v_ref = 3.3f;
    ctx.board_ntc.init(ntc_cfg, NTC_TABLE, sizeof(NTC_TABLE) / sizeof(NTC_TABLE[0]));
    ctx.motor_ntc.init(ntc_cfg, NTC_TABLE, sizeof(NTC_TABLE) / sizeof(NTC_TABLE[0]));

    // Position controller
    PidConfig pos_pid_cfg;
    pos_pid_cfg.kp = static_cast<float>(CONFIG_POS_KP) * 0.001F;
    pos_pid_cfg.ki = static_cast<float>(CONFIG_POS_KI) * 0.001F;
    pos_pid_cfg.kd = static_cast<float>(CONFIG_POS_KD) * 0.001F;
    pos_pid_cfg.ts = 1.0f / POS_LOOP_HZ;
    pos_pid_cfg.i_limit =
        static_cast<float>(CONFIG_POS_OUTPUT_LIMIT) * 0.001F;
    pos_pid_cfg.output_limit = pos_pid_cfg.i_limit;
    BacklashConfig backlash_cfg;
    backlash_cfg.deadzone =
        static_cast<float>(CONFIG_POS_BACKLASH_DEADZONE) * 0.001F;
    backlash_cfg.hysteresis =
        static_cast<float>(CONFIG_POS_BACKLASH_HYSTERESIS) * 0.001F;
    backlash_cfg.cmd_threshold =
        static_cast<float>(CONFIG_POS_BACKLASH_CMD_THRESHOLD) * 0.001F;
    backlash_cfg.dwell_time_s =
        static_cast<float>(CONFIG_POS_BACKLASH_DWELL_TIME) * 0.001F;
    float *out_angle_tbl = ctx.output_angle_calib_done ? ctx.output_angle_table : nullptr;
    ctx.pos_ctrl.init(pos_pid_cfg, backlash_cfg,
                      out_angle_tbl, OUTPUT_ANGLE_TABLE_SIZE,
                      static_cast<float>(POS_LOOP_HZ));
    if (ctx.max_pos_calib_done) {
        ctx.pos_ctrl.set_max_pos_rad(ctx.max_pos_rad);
    }

    // Protection
    ProtectionConfig prot_cfg;
    prot_cfg.bus_overvoltage =
        static_cast<float>(CONFIG_PROTECT_BUS_OV) * 0.001F;
    prot_cfg.bus_undervoltage =
        static_cast<float>(CONFIG_PROTECT_BUS_UV) * 0.001F;
    prot_cfg.bus_ov_recovery = prot_cfg.bus_overvoltage * (42.0F / 45.0F);
    prot_cfg.bus_uv_recovery = prot_cfg.bus_undervoltage * (15.0F / 12.0F);
    prot_cfg.board_overtemp =
        static_cast<float>(CONFIG_PROTECT_BOARD_OT) * 0.1F;
    prot_cfg.motor_overtemp =
        static_cast<float>(CONFIG_PROTECT_MOTOR_OT) * 0.1F;
    prot_cfg.board_ot_recovery = prot_cfg.board_overtemp * 0.5F;
    prot_cfg.motor_ot_recovery = prot_cfg.motor_overtemp * (2.0F / 3.0F);
    prot_cfg.overcurrent = ctx.motor_cfg.imax * 1.2F;
    prot_cfg.fast_trigger_cnt = 20;
    prot_cfg.fast_recover_cnt = 100;
    prot_cfg.slow_trigger_cnt = 2000;
    prot_cfg.slow_recover_cnt = 2000;
    ctx.protection.init(prot_cfg);

    // Speed loop LESO
    SpeedController::Config spd_cfg;
    spd_cfg.leso.type = algo::LesoType::FirstOrder;
    spd_cfg.leso.omega = static_cast<float>(CONFIG_LESO_OMEGA);
    spd_cfg.leso.b0 = static_cast<float>(CONFIG_LESO_B0);
    spd_cfg.leso.ts = 1.0f / SPEED_LOOP_HZ;
    spd_cfg.leso.kp = static_cast<float>(CONFIG_LESO_KP) * 0.001F;
    spd_cfg.leso.output_min = -ctx.motor_cfg.imax;
    spd_cfg.leso.output_max = ctx.motor_cfg.imax;
    spd_cfg.torque_constant = MOTOR_TORQUE_CONSTANT;
    spd_cfg.max_current = ctx.motor_cfg.imax;
    ctx.speed_ctrl.init(spd_cfg);

    // CAN
    ctx.can.init(ctx.can_base_id, app::board::main_can().native());
    ctx.can.set_callback(can_cmd_callback);

    // Speed loop thread, driven by hardware timer TIMER5 (4 kHz).
    auto &speed_tim = app::board::speed_timer();
    osal::PeriodicThreadConfig speed_config {};
    speed_config.name = spd_name;
    speed_config.entry = speed_loop_entry;
    speed_config.context = &ctx;
    speed_config.stack_size_bytes = SPEED_LOOP_STACK;
    speed_config.stack_buffer = runtime.speed_stack;
    speed_config.priority = static_cast<osal::Priority>(SPEED_LOOP_PRIO);
    speed_config.frequency_hz = SPEED_LOOP_HZ;
    speed_config.trigger = osal::PeriodicTrigger::External;
    speed_config.timer = &speed_tim;
    if (!start_periodic_thread(ctx.speed_loop, runtime.speed_thread,
                               speed_config)) {
        LOGE("foc", "failed to start speed loop for motor %d", motor_idx);
        rollback_motor_runtime(ctx);
        return -1;
    }

    // Position loop thread
    osal::PeriodicThreadConfig position_config {};
    position_config.name = pos_name;
    position_config.entry = pos_loop_entry;
    position_config.context = &ctx;
    position_config.stack_size_bytes = POS_LOOP_STACK;
    position_config.stack_buffer = runtime.position_stack;
    position_config.priority = static_cast<osal::Priority>(POS_LOOP_PRIO);
    position_config.frequency_hz = POS_LOOP_HZ;
    if (!start_periodic_thread(ctx.pos_loop, runtime.position_thread,
                               position_config)) {
        LOGE("foc", "failed to start pos loop for motor %d", motor_idx);
        rollback_motor_runtime(ctx);
        return -1;
    }

    // Slow loop thread
    osal::PeriodicThreadConfig slow_config {};
    slow_config.name = slw_name;
    slow_config.entry = slow_loop_entry;
    slow_config.context = &ctx;
    slow_config.stack_size_bytes = SLOW_LOOP_STACK;
    slow_config.stack_buffer = runtime.slow_stack;
    slow_config.priority = static_cast<osal::Priority>(SLOW_LOOP_PRIO);
    slow_config.frequency_hz = SLOW_LOOP_HZ;
    if (!start_periodic_thread(ctx.slow_loop, runtime.slow_thread,
                               slow_config)) {
        LOGE("foc", "failed to start slow loop for motor %d", motor_idx);
        rollback_motor_runtime(ctx);
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
        sc_cfg.stack_buffer = runtime.sensor_stack;
        sc_cfg.priority = static_cast<osal::Priority>(CTRL_LOOP_PRIO);
        sc_cfg.frequency_hz = CTRL_LOOP_HZ;
        sc_cfg.timer = &imu_tim;
// IMU data updated by the SensorCore worker thread.
#ifdef CONFIG_IMU_ICM40609D
        sc_cfg.read_fn = imu_read_sample;
        sc_cfg.divider = 8;
#endif
        if (!runtime.sensor_core.configure(sc_cfg)
            || runtime.sensor_core.start() != 0) {
            LOGE("foc", "failed to start imu core for motor %d", motor_idx);
            rollback_motor_runtime(ctx);
            return -1;
        }
        ctx.imu_core = &runtime.sensor_core;
    }

    if (!ctx.current_calib.calib_done || !ctx.pos_sensor.is_calibrated()) {
        LOGW("foc", "motor %d: calibration data missing; automatic power-stage calibration disabled",
             motor_idx);
    }

    return 0;
}

// Public API

namespace app {

int start_control() {
    if (g_motor_count != 0U && g_led_thread != nullptr) return 0;
    if (g_motor_count != 0U || g_led_thread != nullptr) stop_control();
    if (nvs_flash().init() != hal::Status::Ok) {
        LOGW("nvs", "flash initialization failed; calibration persistence disabled");
    }

    // Initialize motor 0.
    auto &motor_dev = app::board::main_motor();
    auto &ctx0 = g_motors[0];
    ctx0.motor = &motor_dev.motor();
    ctx0.motor_cfg = ctx0.motor->config();
    ctx0.can_base_id = 0x100;
    ctx0.nvs_id_base = 0x0001;
    if (init_motor(ctx0, 0) != 0) {
        g_motor_count = 0;
        (void)nvs_flash().deinit();
        return -1;
    }
    g_motor_count = 1;

#ifdef CONFIG_LINK
    if (!comm_init()) {
        rollback_motor_runtime(ctx0);
        g_motor_count = 0;
        (void)nvs_flash().deinit();
        return -1;
    }
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
    osal::PeriodicThreadConfig led_config {};
    led_config.name = "led_hb";
    led_config.entry = led_heartbeat_entry;
    led_config.stack_size_bytes = LED_STACK;
    led_config.stack_buffer = g_led_stack;
    led_config.priority = static_cast<osal::Priority>(LED_PRIO);
    led_config.frequency_hz = LED_HZ;
    if (!start_periodic_thread(g_led_thread, g_led_thread_storage,
                               led_config)) {
        LOGE("foc", "failed to start LED thread");
#ifdef CONFIG_LINK
        comm_deinit();
#endif
        rollback_motor_runtime(ctx0);
        g_motor_count = 0;
        (void)nvs_flash().deinit();
        return -1;
    }

    LOGI("foc", "FOC system initialized (%d motor(s), %dpp, gear=%d, LESO+PID pos ctrl)",
         g_motor_count, ctx0.motor_cfg.pole_pairs,
         static_cast<int>(MOTOR_GEAR_RATIO));
    return 0;
}

void stop_control() {
    destroy_periodic_thread(g_led_thread);
    for (size_t index = g_motor_count; index > 0U; --index) {
        rollback_motor_runtime(g_motors[index - 1U]);
    }
#ifdef CONFIG_LINK
    comm_deinit();
#endif
    g_motor_count = 0U;
    g_active_motor.store(0U, std::memory_order_release);
    (void)nvs_flash().deinit();
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
