#include "foc_app.h"

#include <device.h>
#include <drivers/gpio.h>
#include <drivers/pwm.h>
#include <drivers/adc.h>
#include <drivers/flash.h>
#include <drivers_generated.h>
#include <init.h>
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

static hal::Flash g_flash(hal::flash_create_gd32());
static nvs::Nvs<hal::Flash> g_nvs(g_flash, NVS_OFFSET);

static osal::PeriodicThread *g_slow_loop = nullptr;
static osal::PeriodicThread *g_led_thread = nullptr;

static char cli_buf[CLI_BUF_SIZE];
static size_t cli_pos = 0;

// ─── NVS 辅助 ───

void nvs_load_config() {
    CalibData cal{};
    int32_t r = g_nvs.read(NVS_ID_CALIB, cal);
    if (r > 0) {
        motor_cfg.rs = cal.rs;
        motor_cfg.ld = cal.ld;
        motor_cfg.lq = cal.lq;
        motor_cfg.flux_linkage = cal.flux_linkage;
        motor_cfg.pole_pairs = cal.pole_pairs;
        LOGI("nvs", "loaded calib: Rs=%.4f Ld=%.6f Lq=%.6f Flux=%.5f pp=%d",
             cal.rs, cal.ld, cal.lq, cal.flux_linkage, cal.pole_pairs);
    } else {
        LOGI("nvs", "no saved calibration, using Kconfig defaults");
    }
}

void nvs_save_config() {
    CalibData cal{};
    cal.rs = motor_cfg.rs;
    cal.ld = motor_cfg.ld;
    cal.lq = motor_cfg.lq;
    cal.flux_linkage = motor_cfg.flux_linkage;
    cal.pole_pairs = motor_cfg.pole_pairs;
    int32_t r = g_nvs.write(NVS_ID_CALIB, cal);
    if (r > 0) {
        LOGI("nvs", "calibration saved: Rs=%.4f Ld=%.6f Lq=%.6f",
             cal.rs, cal.ld, cal.lq);
    } else {
        LOGE("nvs", "save failed (%ld)", static_cast<long>(r));
    }
}

void nvs_reset_config() {
    (void)g_nvs.remove(NVS_ID_CALIB);
    motor_cfg.rs = CONFIG_FOC_RS_OHMS * 0.001f;
    motor_cfg.ld = CONFIG_FOC_LD_HENRIES * 1e-6f;
    motor_cfg.lq = CONFIG_FOC_LQ_HENRIES * 1e-6f;
    motor_cfg.flux_linkage = CONFIG_FOC_FLUX_LINKAGE * 1e-3f;
    motor_cfg.pole_pairs = static_cast<uint8_t>(CONFIG_FOC_POLE_PAIRS);
    LOGI("nvs", "calibration reset to Kconfig defaults");
}

// ─── PWM ISR 回调 (快速循环) ───

void pwm_isr_callback(void *arg) {
    auto *motor = static_cast<foc::Motor *>(arg);
    motor->fast_loop_isr();
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
        LOGI("cli", "  reset             - Reset to Kconfig defaults");
        LOGI("cli", "  estop             - Emergency stop");
        LOGI("cli", "  help              - Show this help");
    } else {
        LOGW("cli", "unknown command: '%s', type 'help'", cmd);
    }
}

// ─── initcall ───

int foc_gpio_init() {
    auto &led = device_get(led0);
    (void)led.configure(GPIO_OUTPUT_LOW);
    led.off();
    return 0;
}

int foc_motor_init() {
    auto &pwm = device_get(pwm0);
    auto &adc = device_get(adc0);

    // Kconfig 用工程单位 (mOhm/uH/mWb)，C++ 用基本单位 (Ohm/H/Wb)
    motor_cfg.rs = CONFIG_FOC_RS_OHMS * 0.001f;
    motor_cfg.ld = CONFIG_FOC_LD_HENRIES * 1e-6f;
    motor_cfg.lq = CONFIG_FOC_LQ_HENRIES * 1e-6f;
    motor_cfg.flux_linkage = CONFIG_FOC_FLUX_LINKAGE * 1e-3f;
    static_assert(CONFIG_FOC_POLE_PAIRS > 0
                  && CONFIG_FOC_POLE_PAIRS <= 255,
                  "pole_pairs out of uint8_t range");
    motor_cfg.pole_pairs = static_cast<uint8_t>(
        CONFIG_FOC_POLE_PAIRS);
    motor_cfg.imax = CONFIG_FOC_MAX_CURRENT_A;
    motor_cfg.vmax = CONFIG_FOC_MAX_VOLTAGE_V;
    motor_cfg.sensor = foc::SensorMode::Sensorless;
    motor_cfg.control = foc::ControlMode::Speed;
    motor_cfg.foc.pwm_frequency = CONFIG_FOC_PWM_FREQUENCY_HZ;
    motor_cfg.foc.current_bandwidth = CONFIG_FOC_CURRENT_LOOP_BW_HZ;

    // NVS：加载标定参数（覆盖 Kconfig 默认值）
    (void)g_flash.init();
    if (g_nvs.mount() == nvs::Status::Ok) {
        nvs_load_config();
    } else {
        LOGW("nvs", "mount failed, using Kconfig defaults");
    }

    static foc::Motor motor(motor_cfg, pwm,
                            hal::PwmChannel::Ch1, hal::PwmChannel::Ch2, hal::PwmChannel::Ch3,
                            adc);
    motor.init();
    g_motor = &motor;

    (void)pwm.set_update_callback(pwm_isr_callback, g_motor);

    return 0;
}

SYS_INIT(foc_gpio_init, INITCALL_LEVEL_PRE_KERNEL_3, 80);
SYS_INIT(foc_motor_init, INITCALL_LEVEL_APPLICATION, 80);

} // namespace

// ─── 公共接口 ───

namespace foc_app {

int start() {
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
