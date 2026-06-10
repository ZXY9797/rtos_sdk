#pragma once

#include <cstdint>

enum class CanCmd : uint8_t {
    Enable          = 0x01,
    Disable         = 0x02,
    SetSpeed        = 0x03,
    SetTorque       = 0x04,
    SetPosition     = 0x05,
    GetStatus       = 0x06,
    Calibrate       = 0x07,
    Estop           = 0x08,
    GetMaxPosRad    = 0x09,
    GetSensorRaw    = 0x0A,
    CalibPosZero    = 0x0B,
    CalibMaxPos     = 0x0C,
};

enum class CanReport : uint8_t {
    Status      = 0x10,
    Telemetry   = 0x11,
    Position    = 0x12,
    Temperature = 0x13,
    MaxPosRad   = 0x14,
    SensorRaw   = 0x15,
};

class CanHandler {
public:
    using CmdCallback = void(*)(CanCmd cmd, const uint8_t *data, uint8_t len);

    void init(uint32_t base_id);
    bool process();

    void report_status(uint8_t state, uint32_t error_flags);
    void report_telemetry(float speed_rpm, float iq, float vbus);
    void report_position(float angle_rad, float accel);
    void report_temperature(float board_t, float motor_t);
    void report_max_pos_rad(float max_rad);
    void report_sensor_raw(float iu, float iv, float iw, float vbus, float angle);

    void set_callback(CmdCallback cb) { callback_ = cb; }

    bool is_ready() const { return ready_; }

private:
    uint32_t base_id_ {0x100};
    bool send(uint32_t id, const uint8_t *data, uint8_t len);
    bool ready_ {false};
    CmdCallback callback_ {nullptr};

    void dispatch(uint32_t id, const uint8_t *data, uint8_t len);
};
