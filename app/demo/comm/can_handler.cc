#include "can_handler.h"

#include <cstring>
#include <algorithm>

#if defined(CONFIG_CAN_GD32) || defined(HAL_CAN_MODULE_ENABLED)
#include <drivers/can.h>
static hal::Can *g_can = nullptr;
#endif

static void float_to_bytes(float val, uint8_t *data) {
    memcpy(data, &val, sizeof(float));
}

void CanHandler::init(uint32_t base_id) {
    base_id_ = base_id;
#if defined(CONFIG_CAN_GD32) || defined(HAL_CAN_MODULE_ENABLED)
    g_can = &hal::Can::instance(0);
    hal::CanConfig cfg;
    cfg.bitrate = 500000U;
    if (g_can->init(cfg) == hal::Status::Ok) {
        (void)g_can->start();
        ready_ = true;
    }
#else
    ready_ = false;
#endif
}

bool CanHandler::process() {
#if defined(CONFIG_CAN_GD32) || defined(HAL_CAN_MODULE_ENABLED)
    if (!ready_ || !g_can) return false;

    uint32_t id = 0;
    uint8_t data[8] = {};
    uint8_t len = 0;

    if (g_can->get_rx_message(&id, data, &len) == hal::Status::Ok) {
        dispatch(id, data, len);
        return true;
    }
#endif
    return false;
}

void CanHandler::dispatch(uint32_t id, const uint8_t *data, uint8_t len) {
    if (len < 1) return;

    auto cmd = static_cast<CanCmd>(data[0]);
    if (callback_) {
        callback_(cmd, data + 1, len - 1);
    }
}

bool CanHandler::send(uint32_t id, const uint8_t *data, uint8_t len) {
#if defined(CONFIG_CAN_GD32) || defined(HAL_CAN_MODULE_ENABLED)
    if (!ready_ || !g_can) return false;
    return g_can->send(id, data, len, 0) == hal::Status::Ok;
#else
    return false;
#endif
}

void CanHandler::report_status(uint8_t state, uint32_t error_flags) {
    uint8_t data[8] = {};
    data[0] = static_cast<uint8_t>(CanReport::Status);
    data[1] = state;
    memcpy(data + 2, &error_flags, 4);
    send(base_id_ + static_cast<uint32_t>(CanReport::Status), data, 6);
}

void CanHandler::report_telemetry(float speed_rpm, float iq, float vbus) {
    uint8_t data[8] = {};
    data[0] = static_cast<uint8_t>(CanReport::Telemetry);
    float_to_bytes(speed_rpm, data + 1);
    data[5] = static_cast<uint8_t>(static_cast<int8_t>(iq * 10.0f));
    data[6] = static_cast<uint8_t>(static_cast<uint8_t>(vbus));
    send(base_id_ + static_cast<uint32_t>(CanReport::Telemetry), data, 7);
}

void CanHandler::report_position(float angle_rad, float accel) {
    uint8_t data[8] = {};
    data[0] = static_cast<uint8_t>(CanReport::Position);
    float_to_bytes(angle_rad, data + 1);
    send(base_id_ + static_cast<uint32_t>(CanReport::Position), data, 5);
}

void CanHandler::report_temperature(float board_t, float motor_t) {
    uint8_t data[4] = {};
    data[0] = static_cast<uint8_t>(CanReport::Temperature);
    data[1] = static_cast<uint8_t>(board_t);
    data[2] = static_cast<uint8_t>(motor_t);
    send(base_id_ + static_cast<uint32_t>(CanReport::Temperature), data, 3);
}

void CanHandler::report_max_pos_rad(float max_rad) {
    uint8_t data[8] = {};
    data[0] = static_cast<uint8_t>(CanReport::MaxPosRad);
    float_to_bytes(max_rad, data + 1);
    send(base_id_ + static_cast<uint32_t>(CanReport::MaxPosRad), data, 5);
}

void CanHandler::report_sensor_raw(float iu, float iv, float iw, float vbus, float angle) {
    uint8_t data[8] = {};
    data[0] = static_cast<uint8_t>(CanReport::SensorRaw);
    data[1] = static_cast<uint8_t>(std::clamp(iu * 10.0f, -128.0f, 127.0f) + 128.0f);
    data[2] = static_cast<uint8_t>(std::clamp(iv * 10.0f, -128.0f, 127.0f) + 128.0f);
    data[3] = static_cast<uint8_t>(std::clamp(iw * 10.0f, -128.0f, 127.0f) + 128.0f);
    data[4] = static_cast<uint8_t>(std::clamp(vbus, 0.0f, 255.0f));
    // angle: 0~2π -> 0~65535 (16-bit)
    uint16_t angle_raw = static_cast<uint16_t>(std::clamp(angle, 0.0f, 6.28318f) * 65535.0f / 6.28318f);
    data[5] = static_cast<uint8_t>(angle_raw & 0xFF);
    data[6] = static_cast<uint8_t>((angle_raw >> 8) & 0xFF);
    send(base_id_ + static_cast<uint32_t>(CanReport::SensorRaw), data, 7);
}
