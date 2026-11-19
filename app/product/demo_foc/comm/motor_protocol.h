#pragma once

#include "control/motor_control.h"
#include <link/frame.h>

namespace app {
inline constexpr uint8_t kMotorCommandSet = 0x30U;
enum class MotorCommand : uint8_t {
    Status, Enable, Mode, Target, Calibrate, Stop, ClearFault, Backlash,
    Stream, Rl, Sweep, Lut, JointZero, JointOffset, CurrentLinear, BusLinear, Plan,
    CalibrationData, Observe, ModeTransaction, ModeStatus, Telemetry, Capture,
    InteractionConfig, ExternalFeedback, InteractionStatus,
    ElectricalModel, TorqueCalibration,
};

struct MotorRequest {
    MotorCommand command {};
    uint16_t owner {0U};
    uint16_t size {0U};
    link::CompletionToken completion {};
    uint8_t data[496] {};
};

struct MotorResponse {
    uint8_t data[496] {};
    size_t size {0U};
    bool byte(uint8_t value);
    bool u16(uint16_t value);
    bool u32(uint32_t value);
    bool real(float value);
};

uint16_t read_u16(const uint8_t* bytes);
uint32_t read_u32(const uint8_t* bytes);
float read_float(const uint8_t* bytes);
void encode_status(const MotorStatus& status, MotorResponse& response);
void encode_rl(const algo::motor::RlIdentificationBurstPacket& packet, MotorResponse& response);
void encode_sweep(const algo::motor::CurrentSweepBurstPacket& packet, MotorResponse& response);
bool decode_mode_command(const uint8_t* data, size_t size, ModeCommand& command);
void encode_mode_status(const ModeStatus& status, MotorResponse& response);
bool encode_calibration(const RecordView& record, uint16_t offset, uint16_t count,
                        MotorResponse& response);
bool encode_observation(uint8_t kind, const MotorObservation& sample, MotorResponse& response);
void encode_capture(const CalibrationPair& pair, MotorResponse& response);
bool encode_interaction_section(InteractionConfig config, uint8_t section, MotorResponse& response);
bool decode_interaction_section(const uint8_t* bytes, size_t size, InteractionConfig& config);
size_t pack_interaction_config(InteractionConfig config, uint8_t* bytes, size_t capacity);
bool unpack_interaction_config(const uint8_t* bytes, size_t size, InteractionConfig& config);
bool enqueue_motor_request(const MotorRequest& request);
bool start_motor_transport();
bool motor_stream_ready(uint8_t owner);
bool motor_log_backend(void* context, const char* text, size_t size);
} // namespace app
