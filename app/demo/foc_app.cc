#include "foc_app.h"

#include <device.h>
#include <drivers/flash.h>
#include <drivers_generated.h>
#include <log.h>
#include <osal.h>
#include <irq.h>

#include <nvs/nvs.h>
#include <foc/motor.h>
#include <foc/config.h>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <cmath>

namespace {

// ─── 常量 ───

constexpr uint32_t SLOW_LOOP_HZ = CONFIG_FOC_SPEED_LOOP_HZ;
constexpr size_t SLOW_LOOP_STACK_SIZE = 2048;
constexpr int32_t SLOW_LOOP_PRIORITY = 6;

constexpr uint32_t LED_HZ = 2;
constexpr size_t LED_STACK_SIZE = 512;
constexpr int32_t LED_PRIORITY = 10;

constexpr size_t CLI_BUF_SIZE = 128;

// NVS：Flash 最后 3 页（6KB）
constexpr uint32_t NVS_OFFSET = 512U * 1024U - 3U * 2048U;
constexpr uint16_t NVS_ID_CALIB = 0x0001;

// ─── 标定数据 ───

struct CalibData {
    float rs;
    float ld;
    float lq;
    float flux_linkage;
    uint8_t pole_pairs;
    uint8_t reserved[3];
};
static_assert(sizeof(CalibData) == 20);

// ─── 状态 ───

static foc::MotorConfig motor_cfg;
static foc::Motor *g_motor = nullptr;

static osal::PeriodicThread *g_slow_loop = nullptr;
static osal::PeriodicThread *g_led_thread = nullptr;

static char cli_buf[CLI_BUF_SIZE];
static size_t cli_pos = 0;

// ─── NVS 辅助 ───

static hal::Flash &nvs_flash() {
    static hal::Flash flash(hal::flash_create_gd32());
    return flash;
}

static nvs::Nvs<hal::Flash> &nvs_store() {
    static nvs::Nvs<hal::Flash> instance(nvs_flash(), NVS_OFFSET);
    return instance;
}

bool nvs_load_config() {
    CalibData cal{};
    int32_t r = nvs_store().read(NVS_ID_CALIB, cal);
    if (r > 0) {
        motor_cfg.rs = cal.rs;
        motor_cfg.ld = cal.ld;
        motor_cfg.lq = cal.lq;
        motor_cfg.flux_linkage = cal.flux_linkage;
        motor_cfg.pole_pairs = cal.pole_pairs;
        LOGI("nvs", "loaded calib: Rs=%.4f Ld=%.6f Lq=%.6f Flux=%.5f pp=%d",
             cal.rs, cal.ld, cal.lq, cal.flux_linkage, cal.pole_pairs);
        return true;
    }
    LOGI("nvs", "no saved calibration, using DTS defaults");
    return false;
}

void nvs_save_config() {
    CalibData cal{};
    cal.rs = motor_cfg.rs;
    cal.ld = motor_cfg.ld;
    cal.lq = motor_cfg.lq;
    cal.flux_linkage = motor_cfg.flux_linkage;
    cal.pole_pairs = motor_cfg.pole_pairs;
    int32_t r = nvs_store().write(NVS_ID_CALIB, cal);
    if (r > 0) {
        LOGI("nvs", "calibration saved: Rs=%.4f Ld=%.6f Lq=%.6f",
             cal.rs, cal.ld, cal.lq);
    } else {
        LOGE("nvs", "save failed (%ld)", static_cast<long>(r));
    }
}

void nvs_reset_config() {
    (void)nvs_store().remove(NVS_ID_CALIB);
    LOGI("nvs", "calibration cleared — reboot to apply DTS defaults");
}

// ─── 慢循环 ───

void slow_loop_entry(void *, const osal::PeriodicStats &stats) {
    if (!g_motor) return;
    g_motor->slow_loop();

    // 测量完成后自动保存
    if (g_motor->measurement().is_done()) {
        auto &meas = g_motor->measurement();
        motor_cfg.rs = meas.rs();
        motor_cfg.ld = meas.ld();
        motor_cfg.lq = meas.lq();
        motor_cfg.flux_linkage = meas.flux_linkage();
        nvs_save_config();
    }

    if ((stats.sequence % SLOW_LOOP_HZ) == 0) {
        LOGI("foc", "speed=%.1f rpm, id=%.2f, iq=%.2f, vbus=%.1fV, state=%d",
             g_motor->speed_rpm(),
             g_motor->foc_controller().id(),
             g_motor->foc_controller().iq(),
             g_motor->bus_voltage(),
             static_cast<int>(g_motor->state()));
    }
}

// ─── LED 心跳 ───

void led_heartbeat_entry(void *, const osal::PeriodicStats &stats) {
    auto &led = device_get(led0);
    const auto *motor = g_motor;
    if (!motor) return;

    switch (motor->state()) {
    case foc::MotorState::Running:
        led.toggle();
        break;
    case foc::MotorState::Error:
        led.on();
        break;
    default:
        if ((stats.sequence % LED_HZ) == 0) led.toggle();
        break;
    }
}

// ─── CLI 命令 ───

void cli_process(const char *cmd) {
    if (strcmp(cmd, "start") == 0 || strcmp(cmd, "en") == 0) {
        if (g_motor) {
            g_motor->enable();
            LOGI("cli", "motor enabled");
        }
    } else if (strcmp(cmd, "stop") == 0 || strcmp(cmd, "dis") == 0) {
        if (g_motor) {
            g_motor->disable();
            LOGI("cli", "motor disabled");
        }
    } else if (strncmp(cmd, "speed ", 6) == 0) {
        float rpm = 0;
        if (sscanf(cmd + 6, "%f", &rpm) == 1 && g_motor
            && std::isfinite(rpm) && rpm >= 0.0f
            && rpm <= CONFIG_FOC_MAX_SPEED_RPM) {
            g_motor->set_speed(rpm);
            LOGI("cli", "speed set to %.1f rpm", rpm);
        } else {
            LOGW("cli", "invalid speed (0-%.0f rpm)",
                 CONFIG_FOC_MAX_SPEED_RPM);
        }
    } else if (strncmp(cmd, "torque ", 7) == 0) {
        float iq = 0;
        if (sscanf(cmd + 7, "%f", &iq) == 1 && g_motor
            && std::isfinite(iq)
            && iq >= -CONFIG_FOC_MAX_CURRENT_A
            && iq <= CONFIG_FOC_MAX_CURRENT_A) {
            g_motor->set_torque(iq);
            LOGI("cli", "torque set to %.2f A", iq);
        } else {
            LOGW("cli", "invalid torque (-%.1f to +%.1f A)",
                 CONFIG_FOC_MAX_CURRENT_A,
                 CONFIG_FOC_MAX_CURRENT_A);
        }
    } else if (strcmp(cmd, "status") == 0 || strcmp(cmd, "st") == 0) {
        if (g_motor) {
            auto &foc = g_motor->foc_controller();
            LOGI("cli", "=== Motor Status ===");
            LOGI("cli", "  State:   %d", static_cast<int>(g_motor->state()));
            LOGI("cli", "  Speed:   %.1f rpm", g_motor->speed_rpm());
            LOGI("cli", "  Id:      %.3f A", foc.id());
            LOGI("cli", "  Iq:      %.3f A", foc.iq());
            LOGI("cli", "  Vd:      %.3f V", foc.vd());
            LOGI("cli", "  Vq:      %.3f V", foc.vq());
            LOGI("cli", "  Vbus:    %.2f V", g_motor->bus_voltage());
            LOGI("cli", "  Temp:    %.1f C", g_motor->temperature());
            LOGI("cli", "  Errors:  0x%08lx",
                 static_cast<unsigned long>(g_motor->errors().error_flags()));
            LOGI("cli", "  Rs:      %.4f ohm", motor_cfg.rs);
            LOGI("cli", "  Ld:      %.6f H", motor_cfg.ld);
            LOGI("cli", "  Lq:      %.6f H", motor_cfg.lq);
            LOGI("cli", "  Flux:    %.5f Wb", motor_cfg.flux_linkage);
        }
    } else if (strcmp(cmd, "measure") == 0 || strcmp(cmd, "meas") == 0) {
        if (g_motor) {
            g_motor->start_measurement();
            LOGI("cli", "motor parameter measurement started");
        }
    } else if (strcmp(cmd, "save") == 0) {
        nvs_save_config();
    } else if (strcmp(cmd, "reset") == 0) {
        nvs_reset_config();
    } else if (strcmp(cmd, "estop") == 0) {
        if (g_motor) {
            g_motor->emergency_stop();
            LOGI("cli", "EMERGENCY STOP");
        }
    } else if (strcmp(cmd, "help") == 0) {
        LOGI("cli", "=== FOC CLI Commands ===");
        LOGI("cli", "  start / en       - Enable motor");
        LOGI("cli", "  stop / dis        - Disable motor");
        LOGI("cli", "  speed <rpm>       - Set speed (RPM)");
        LOGI("cli", "  torque <A>        - Set torque current");
        LOGI("cli", "  status / st       - Show status");
        LOGI("cli", "  measure / meas    - Auto-measure motor params");
        LOGI("cli", "  save              - Save calibration to NVS");
        LOGI("cli", "  reset             - Reset to DTS defaults");
        LOGI("cli", "  estop             - Emergency stop");
        LOGI("cli", "  help              - Show this help");
    } else {
        LOGW("cli", "unknown command: '%s', type 'help'", cmd);
    }
}

} // namespace

// ─── 公共接口 ───

namespace foc_app {

int start() {
    // motor 已由设备树 initcall 自动初始化
    auto &motor_dev = device_get(motor0);
    g_motor = &motor_dev.motor();

    // 以 DTS 默认值初始化 motor_cfg（供 NVS/CLI 使用）
    motor_cfg.rs = CONFIG_FOC_RS_OHMS * 0.001f;
    motor_cfg.ld = CONFIG_FOC_LD_HENRIES * 1e-6f;
    motor_cfg.lq = CONFIG_FOC_LQ_HENRIES * 1e-6f;
    motor_cfg.flux_linkage = CONFIG_FOC_FLUX_LINKAGE * 1e-3f;
    motor_cfg.pole_pairs = static_cast<uint8_t>(CONFIG_FOC_POLE_PAIRS);

    // NVS：加载标定参数（覆盖 DTS 默认值）
    (void)nvs_flash().init();
    if (nvs_store().mount() == nvs::Status::Ok) {
        if (nvs_load_config()) {
            // 应用 NVS 标定到 FOC 控制器
            g_motor->foc_controller().calculate_gains(
                motor_cfg.rs, motor_cfg.ld, motor_cfg.lq,
                motor_cfg.flux_linkage, motor_cfg.pole_pairs);
        }
    } else {
        LOGW("nvs", "mount failed, using DTS defaults");
    }

    g_slow_loop = osal::PeriodicThread::create("foc_slow",
                                                slow_loop_entry,
                                                nullptr,
                                                SLOW_LOOP_STACK_SIZE,
                                                SLOW_LOOP_PRIORITY,
                                                SLOW_LOOP_HZ,
                                                osal::PeriodicTrigger::Tick);
    if (!g_slow_loop || g_slow_loop->startup() != 0) {
        LOGE("foc", "failed to start slow loop thread");
        return -1;
    }

    g_led_thread = osal::PeriodicThread::create("led_hb",
                                                 led_heartbeat_entry,
                                                 nullptr,
                                                 LED_STACK_SIZE,
                                                 LED_PRIORITY,
                                                 LED_HZ,
                                                 osal::PeriodicTrigger::Tick);
    if (!g_led_thread || g_led_thread->startup() != 0) {
        LOGE("foc", "failed to start LED thread");
    }

    // TIMER0_UP_TIMER9_IRQn (GD32F50x CMSIS)
    constexpr int TIMER0_UP_IRQn = 25;
    hal::Irq::enable(TIMER0_UP_IRQn);
    return 0;
}

void feed_char(char c) {
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

} // namespace foc_app
