#pragma once
#include <algo/motor/status.h>
#include <algo/foc/current_loop.h>
#include <algo/signal/sweep_freq.h>
#include <cstddef>

namespace algo::motor {
inline constexpr size_t kRlIdentificationSamplesPerBurst = 20U;
inline constexpr uint8_t kRlIdentificationCancelledRet = 0x0EU;
inline constexpr size_t kCurrentSweepSamplesPerBurst = 40U;
inline constexpr uint8_t kCurrentSweepCancelledRet = 0x0EU;
enum class RlIdentificationStage : uint8_t {
    Idle = 0U,
    Prepare = 1U,
    Resistance = 2U,
    Inductance = 3U,
    Drain = 4U,
};

struct RlIdentificationBurstPacket {
    bool valid {false};
    uint16_t dst_addr {0U};
    uint8_t version {1U};
    RlIdentificationStage stage {RlIdentificationStage::Idle};
    uint8_t flags {0U};
    uint8_t sample_count {0U};
    uint16_t session_id {0U};
    uint16_t packet_seq {0U};
    uint32_t first_sample_seq {0U};
    uint32_t first_timestamp_us {0U};
    int16_t applied_voltage_mv {0};
    uint16_t vbus_mv {0U};
    int16_t current_alpha_ma[kRlIdentificationSamplesPerBurst] {};
};

struct RlIdentificationResultSnapshot {
    bool finished {false};
    uint8_t ret {0U};
};

enum class CurrentSweepStage : uint8_t {
    Preload = 1U,
    Sweep = 2U,
};

struct CurrentSweepConfig {
    float q_bias_current_a {1.0F};
    float amplitude_a {0.3F};
    float start_frequency_hz {2.0F};
    float end_frequency_hz {2000.0F};
    float step_frequency_hz {2.0F};
    uint8_t repeat_cycles {4U};
    uint32_t preload_duration_ms {500U};
};

struct CurrentSweepBurstPacket {
    bool valid {false};
    uint16_t dst_addr {0U};
    uint8_t version {2U};
    CurrentSweepStage stage {CurrentSweepStage::Preload};
    uint8_t flags {0U};
    uint8_t sample_count {0U};
    uint16_t session_id {0U};
    uint16_t packet_seq {0U};
    uint32_t first_sample_seq {0U};
    uint32_t first_frequency_sample_index {0U};
    uint32_t frequency_millihz {0U};
    int16_t q_bias_current_ma {0};
    uint16_t amplitude_ma {0U};
    int16_t feedback_current_ma[kCurrentSweepSamplesPerBurst] {};
};

struct CurrentSweepResultSnapshot {
    bool finished {false};
    uint8_t ret {0U};
};


struct DiagnosticGates {
    bool control_disabled {false};
    bool calibration_idle {false};
    bool stream_ready {false};
    bool protection_clear {false};
};

struct DiagnosticDrive {
    bool energize {false};
    CurrentLoopMode mode {CurrentLoopMode::Disabled};
    foc::Dq target_current_a {};
    foc::AlphaBeta voltage_v {};
};

// Explicit 20 kHz sample and 1 ms tick. Serialize calls externally if they run
// in different contexts. Peek/consume retain a burst until transport accepts it.
class MotorDiagnostics {
public:
    bool start_rl(const DiagnosticGates& gates);
    bool start_sweep(const CurrentSweepConfig& config, float sample_frequency_hz,
                     const DiagnosticGates& gates);
    void stop();
    DiagnosticDrive sample(float raw_current_alpha_a, float filtered_q_current_a,
                           float calibrated_bus_voltage_v);
    void tick_1ms(bool power_stage_ready, bool hardware_fault = false);
    bool active() const { return m_rl_identification.active || m_current_sweep.active; }
    RlIdentificationStage rl_stage() const { return m_rl_identification.stage; }
    RlIdentificationResultSnapshot rl_result() const {
        return {m_rl_identification.finished, m_rl_identification.ret};
    }
    CurrentSweepResultSnapshot sweep_result() const {
        return {m_current_sweep.finished, m_current_sweep.ret};
    }
    uint32_t dropped_rl_bursts() const { return m_rl_identification.dropped_bursts; }
    uint32_t dropped_sweep_bursts() const { return m_current_sweep.dropped_bursts; }
    bool peek_rl_identification_burst(
    uint16_t dst_addr,
    RlIdentificationBurstPacket* packet) const;
    void consume_rl_identification_burst(uint16_t packet_seq);
    bool peek_current_sweep_burst(
    uint16_t dst_addr,
    CurrentSweepBurstPacket* packet) const;
    void consume_current_sweep_burst(uint16_t packet_seq);

private:
    bool flush_rl_identification_active_burst();
    void capture_rl_identification_sample_from_isr(
    float current_alpha_a,
    float applied_voltage_v,
    float vbus_v);
    bool flush_current_sweep_active_burst();
    void capture_current_sweep_sample_from_isr(
    float feedback_current_a,
    uint32_t frequency_millihz,
    CurrentSweepStage stage);
    void finish_current_sweep(uint8_t ret);
    void current_sweep_slow_task();
    void finish_rl_identification(uint8_t ret);
    void rl_identification_slow_task();
    static constexpr size_t kRlIdentificationBurstQueueCount = 8U;

    struct RlIdentificationRuntime {
        bool active {false};
        bool finished {false};
        bool stop_requested {false};
        bool overflow {false};
        bool safety_fault {false};
        uint8_t ret {0U};
        uint16_t session_id {0U};
        uint16_t next_packet_seq {0U};
        RlIdentificationStage stage {RlIdentificationStage::Idle};
        uint32_t stage_elapsed_ms {0U};
        uint32_t total_elapsed_ms {0U};
        uint32_t drain_elapsed_ms {0U};
        uint32_t sample_seq {0U};
        uint32_t inductance_excitation_samples {0U};
        uint32_t dropped_bursts {0U};
        float resistance_current_target_a {0.0F};
        float resistance_integral_v {0.0F};
        float active_voltage_sum_v {0.0F};
        float active_vbus_sum_v {0.0F};
        RlIdentificationBurstPacket active_burst {};
        RlIdentificationBurstPacket burst_queue[kRlIdentificationBurstQueueCount] {};
        uint8_t burst_head {0U};
        uint8_t burst_tail {0U};
        uint8_t burst_count {0U};
    };

    static constexpr size_t kCurrentSweepBurstQueueCount = 8U;

    struct CurrentSweepRuntime {
        bool active {false};
        bool finished {false};
        bool stop_requested {false};
        bool safety_fault {false};
        bool generator_complete {false};
        bool draining {false};
        uint8_t ret {0U};
        uint16_t session_id {0U};
        uint16_t next_packet_seq {0U};
        uint32_t total_elapsed_ms {0U};
        uint32_t drain_elapsed_ms {0U};
        uint32_t sample_seq {0U};
        uint32_t dropped_bursts {0U};
        uint32_t current_frequency_millihz {0U};
        uint32_t frequency_sample_index {0U};
        uint32_t preload_sample_count {0U};
        uint32_t preload_sample_target {0U};
        CurrentSweepStage stage {CurrentSweepStage::Preload};
        CurrentSweepConfig config {};
        algo::motor::controller::FreqSweep generator {};
        CurrentSweepBurstPacket active_burst {};
        CurrentSweepBurstPacket burst_queue[kCurrentSweepBurstQueueCount] {};
        uint8_t burst_head {0U};
        uint8_t burst_tail {0U};
        uint8_t burst_count {0U};
    };
    bool output_pending() const { return !power_stage_ready_; }
    bool protection_active() const { return hardware_fault_; }
    RlIdentificationRuntime m_rl_identification {};
    CurrentSweepRuntime m_current_sweep {};
    DiagnosticDrive drive_ {};
    bool power_stage_ready_ {true};
    bool hardware_fault_ {false};
};
}  // namespace algo::motor
