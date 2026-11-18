#include <algo/motor/diagnostics.h>
#include <algo/math/basic_math.h>
#include <cmath>

namespace algo::motor {
namespace {
constexpr float kRlControlPeriodS = 0.00005F;
constexpr uint32_t kRlSamplePeriodUs = 50U;
constexpr uint32_t kRlPrepareDurationMs = 100U;
constexpr uint32_t kRlResistancePointDurationMs = 250U;
constexpr uint32_t kRlResistanceDurationMs = 4U * kRlResistancePointDurationMs;
constexpr uint32_t kRlInductanceSettleDurationMs = 50U;
constexpr uint32_t kRlInductanceExcitationDurationMs = 400U;
constexpr uint32_t kRlInductanceDurationMs = kRlInductanceSettleDurationMs +
    kRlInductanceExcitationDurationMs;
constexpr uint32_t kRlRunTimeoutMs = kRlPrepareDurationMs +
    kRlResistanceDurationMs + kRlInductanceDurationMs + 500U;
constexpr uint32_t kRlDrainTimeoutMs = 2000U;
constexpr float kRlResistanceCurrentTargetsA[] = {0.25F, -0.25F, 0.5F, -0.5F};
constexpr float kRlResistanceKp = 0.3F;
constexpr float kRlResistanceKi = 50.0F;
constexpr float kRlResistanceVoltageLimitV = 1.0F;
constexpr float kRlInductanceVoltageV = 0.5F;
constexpr float kRlAbsoluteCurrentLimitA = 2.0F;
constexpr float kCurrentSweepSampleFrequencyHz = 20000.0F;
constexpr uint32_t kCurrentSweepRunTimeoutMs = 90000U;
constexpr uint32_t kCurrentSweepDrainTimeoutMs = 2000U;
constexpr float kCurrentSweepMaxFrequencyHz = 2000.0F;
constexpr float kCurrentSweepMaxAmplitudeA = 0.5F;
constexpr float kCurrentSweepAbsoluteCurrentLimitA = 2.0F;
constexpr uint32_t kCurrentSweepMaxPreloadDurationMs = 5000U;

bool gates_ready(const DiagnosticGates& gates)
{
    return gates.control_disabled && gates.calibration_idle && gates.stream_ready &&
           gates.protection_clear;
}
}

bool MotorDiagnostics::start_rl(const DiagnosticGates& gates)
{
    if (active() || !gates_ready(gates)) return false;
    uint16_t session = static_cast<uint16_t>(m_rl_identification.session_id + 1U);
    if (!session) session = 1U;
    m_rl_identification = RlIdentificationRuntime {};
    m_rl_identification.active = true;
    m_rl_identification.session_id = session;
    m_rl_identification.stage = RlIdentificationStage::Prepare;
    hardware_fault_ = false;
    drive_ = {};
    return true;
}

bool MotorDiagnostics::start_sweep(const CurrentSweepConfig& config,
                                   float sample_frequency_hz, const DiagnosticGates& gates)
{
    if (active() || !gates_ready(gates)) return false;
    const float finite_check = config.q_bias_current_a + config.amplitude_a +
        config.start_frequency_hz + config.end_frequency_hz + config.step_frequency_hz;
    if (!std::isfinite(finite_check) || !std::isfinite(sample_frequency_hz) ||
        config.q_bias_current_a <= config.amplitude_a || config.amplitude_a <= 0.0F ||
        config.amplitude_a > kCurrentSweepMaxAmplitudeA ||
        config.q_bias_current_a + config.amplitude_a > kCurrentSweepAbsoluteCurrentLimitA ||
        config.start_frequency_hz < 2.0F || config.end_frequency_hz < config.start_frequency_hz ||
        config.end_frequency_hz > kCurrentSweepMaxFrequencyHz || config.step_frequency_hz < 1.0F ||
        config.repeat_cycles == 0U || config.repeat_cycles > 20U ||
        config.preload_duration_ms == 0U || config.preload_duration_ms > kCurrentSweepMaxPreloadDurationMs ||
        std::fabs(sample_frequency_hz - kCurrentSweepSampleFrequencyHz) > 1.0F) return false;
    controller::FreqSweep generator;
    if (!generator.configure({kCurrentSweepSampleFrequencyHz, config.start_frequency_hz,
        config.end_frequency_hz, config.step_frequency_hz, config.amplitude_a,
        config.repeat_cycles})) return false;
    uint16_t session = static_cast<uint16_t>(m_current_sweep.session_id + 1U);
    if (!session) session = 1U;
    m_current_sweep = CurrentSweepRuntime {};
    m_current_sweep.active = true;
    m_current_sweep.session_id = session;
    m_current_sweep.config = config;
    m_current_sweep.generator = generator;
    m_current_sweep.preload_sample_target = static_cast<uint32_t>(
        sample_frequency_hz * config.preload_duration_ms * 0.001F + 0.5F);
    hardware_fault_ = false;
    drive_ = {};
    return true;
}

void MotorDiagnostics::stop()
{
    if (m_rl_identification.active && m_rl_identification.stage != RlIdentificationStage::Drain)
        m_rl_identification.stop_requested = true;
    if (m_current_sweep.active && !m_current_sweep.draining)
        m_current_sweep.stop_requested = true;
}

DiagnosticDrive MotorDiagnostics::sample(float raw_current_alpha_a,
                                          float filtered_q_current_a,
                                          float calibrated_bus_voltage_v)
{
    drive_ = {};
    if (!power_stage_ready_) return drive_;
    if (m_rl_identification.active && m_rl_identification.stage != RlIdentificationStage::Drain) {
        if (!std::isfinite(raw_current_alpha_a) || std::fabs(raw_current_alpha_a) > kRlAbsoluteCurrentLimitA ||
            !std::isfinite(calibrated_bus_voltage_v)) {
            m_rl_identification.safety_fault = true;
            return drive_;
        }
        float voltage = 0.0F;
        if (m_rl_identification.stage == RlIdentificationStage::Resistance) {
            const float error = m_rl_identification.resistance_current_target_a - raw_current_alpha_a;
            m_rl_identification.resistance_integral_v += kRlResistanceKi * error * kRlControlPeriodS;
            m_rl_identification.resistance_integral_v = basic_math::clamp_float(
                m_rl_identification.resistance_integral_v, -kRlResistanceVoltageLimitV, kRlResistanceVoltageLimitV);
            voltage = basic_math::clamp_float(kRlResistanceKp * error + m_rl_identification.resistance_integral_v,
                -kRlResistanceVoltageLimitV, kRlResistanceVoltageLimitV);
        } else if (m_rl_identification.stage == RlIdentificationStage::Inductance &&
                   m_rl_identification.stage_elapsed_ms >= kRlInductanceSettleDurationMs) {
            const bool positive = ((m_rl_identification.inductance_excitation_samples /
                kRlIdentificationSamplesPerBurst) & 1U) == 0U;
            voltage = positive ? kRlInductanceVoltageV : -kRlInductanceVoltageV;
            ++m_rl_identification.inductance_excitation_samples;
        }
        drive_.energize = true;
        drive_.mode = CurrentLoopMode::DirectAlphaBeta;
        drive_.voltage_v.alpha = voltage;
        capture_rl_identification_sample_from_isr(raw_current_alpha_a, voltage, calibrated_bus_voltage_v);
    } else if (m_current_sweep.active && !m_current_sweep.draining) {
        if (!std::isfinite(filtered_q_current_a) ||
            std::fabs(filtered_q_current_a) > kCurrentSweepAbsoluteCurrentLimitA) {
            m_current_sweep.safety_fault = true;
            return drive_;
        }
        CurrentSweepStage stage = CurrentSweepStage::Preload;
        float excitation = 0.0F, frequency = 0.0F;
        if (m_current_sweep.preload_sample_count >= m_current_sweep.preload_sample_target) {
            stage = CurrentSweepStage::Sweep;
            if (m_current_sweep.generator.update()) m_current_sweep.generator_complete = true;
            excitation = m_current_sweep.generator.output();
            frequency = m_current_sweep.generator.output_freq_hz();
        } else ++m_current_sweep.preload_sample_count;
        drive_.energize = true;
        drive_.mode = CurrentLoopMode::Current;
        drive_.target_current_a.q = m_current_sweep.config.q_bias_current_a + excitation;
        capture_current_sweep_sample_from_isr(filtered_q_current_a,
            static_cast<uint32_t>(frequency * 1000.0F + 0.5F), stage);
    }
    return drive_;
}

void MotorDiagnostics::tick_1ms(bool power_stage_ready, bool hardware_fault)
{
    power_stage_ready_ = power_stage_ready;
    hardware_fault_ = hardware_fault;
    rl_identification_slow_task();
    current_sweep_slow_task();
}

bool MotorDiagnostics::peek_rl_identification_burst(
    uint16_t dst_addr,
    RlIdentificationBurstPacket* packet) const
{
    if ((packet == nullptr) || (dst_addr == 0U)) {
        return false;
    }

    if (m_rl_identification.burst_count == 0U) {
        return false;
    }
    *packet = m_rl_identification.burst_queue[
        m_rl_identification.burst_tail];
    packet->dst_addr = dst_addr;
    packet->valid = true;
    return true;
}

void MotorDiagnostics::consume_rl_identification_burst(uint16_t packet_seq)
{

    if (m_rl_identification.burst_count == 0U) {
        return;
    }
    const auto& packet = m_rl_identification.burst_queue[
        m_rl_identification.burst_tail];
    if (packet.packet_seq != packet_seq) {
        return;
    }
    m_rl_identification.burst_queue[m_rl_identification.burst_tail] = {};
    m_rl_identification.burst_tail = static_cast<uint8_t>(
        (m_rl_identification.burst_tail + 1U) %
        kRlIdentificationBurstQueueCount);
    --m_rl_identification.burst_count;
}

bool MotorDiagnostics::flush_rl_identification_active_burst()
{
    // Called either from the current-control ISR or with interrupts masked by
    // ContextCriticalSectionGuard. This lets a slow-task stage boundary close
    // a partial burst without waiting for two independent 1 ms phases to align.
    auto& runtime = m_rl_identification;
    auto& burst = runtime.active_burst;
    if (burst.sample_count == 0U) {
        return true;
    }

    const float sample_count = static_cast<float>(burst.sample_count);
    float voltage_mv = runtime.active_voltage_sum_v * 1000.0F / sample_count;
    float vbus_mv = runtime.active_vbus_sum_v * 1000.0F / sample_count;
    voltage_mv = algo::motor::basic_math::clamp_float(voltage_mv, -32768.0F, 32767.0F);
    vbus_mv = algo::motor::basic_math::clamp_float(vbus_mv, 0.0F, 65535.0F);
    burst.applied_voltage_mv = static_cast<int16_t>(
        voltage_mv + ((voltage_mv >= 0.0F) ? 0.5F : -0.5F));
    burst.vbus_mv = static_cast<uint16_t>(vbus_mv + 0.5F);

    if (runtime.burst_count >= kRlIdentificationBurstQueueCount) {
        ++runtime.dropped_bursts;
        runtime.overflow = true;
        burst = {};
        runtime.active_voltage_sum_v = 0.0F;
        runtime.active_vbus_sum_v = 0.0F;
        return false;
    }
    runtime.burst_queue[runtime.burst_head] = burst;
    runtime.burst_head = static_cast<uint8_t>(
        (runtime.burst_head + 1U) % kRlIdentificationBurstQueueCount);
    ++runtime.burst_count;
    burst = {};
    runtime.active_voltage_sum_v = 0.0F;
    runtime.active_vbus_sum_v = 0.0F;
    return true;
}

void MotorDiagnostics::capture_rl_identification_sample_from_isr(
    float current_alpha_a,
    float applied_voltage_v,
    float vbus_v)
{
    auto& runtime = m_rl_identification;
    if (!runtime.active ||
        ((runtime.stage != RlIdentificationStage::Resistance) &&
         (runtime.stage != RlIdentificationStage::Inductance))) {
        return;
    }
    // current_alpha_a is validated immediately before this private ISR helper
    // is called; retain the independent voltage checks used for packet safety.
    if (!algo::motor::basic_math::is_finite_float(applied_voltage_v) ||
        !algo::motor::basic_math::is_finite_float(vbus_v)) {
        runtime.safety_fault = true;
        return;
    }

    auto& burst = runtime.active_burst;
    if (burst.sample_count == 0U) {
        burst = {};
        burst.valid = true;
        burst.version = 1U;
        burst.stage = runtime.stage;
        burst.session_id = runtime.session_id;
        burst.packet_seq = runtime.next_packet_seq++;
        burst.first_sample_seq = runtime.sample_seq;
        burst.first_timestamp_us = runtime.sample_seq * kRlSamplePeriodUs;
        runtime.active_voltage_sum_v = 0.0F;
        runtime.active_vbus_sum_v = 0.0F;
    }

    float current_ma_f = current_alpha_a * 1000.0F;
    if (current_ma_f > 32767.0F) {
        current_ma_f = 32767.0F;
    } else if (current_ma_f < -32768.0F) {
        current_ma_f = -32768.0F;
    }
    burst.current_alpha_ma[burst.sample_count] =
        static_cast<int16_t>(current_ma_f +
                             ((current_ma_f >= 0.0F) ? 0.5F : -0.5F));
    runtime.active_voltage_sum_v += applied_voltage_v;
    runtime.active_vbus_sum_v += vbus_v;
    ++burst.sample_count;
    ++runtime.sample_seq;

    if (burst.sample_count < kRlIdentificationSamplesPerBurst) {
        return;
    }
    (void)flush_rl_identification_active_burst();
}

bool MotorDiagnostics::peek_current_sweep_burst(
    uint16_t dst_addr,
    CurrentSweepBurstPacket* packet) const
{
    if ((packet == nullptr) || (dst_addr == 0U)) {
        return false;
    }

    if (m_current_sweep.burst_count == 0U) {
        return false;
    }
    *packet = m_current_sweep.burst_queue[m_current_sweep.burst_tail];
    packet->dst_addr = dst_addr;
    packet->valid = true;
    return true;
}

void MotorDiagnostics::consume_current_sweep_burst(uint16_t packet_seq)
{

    if (m_current_sweep.burst_count == 0U) {
        return;
    }
    const auto& packet =
        m_current_sweep.burst_queue[m_current_sweep.burst_tail];
    if (packet.packet_seq != packet_seq) {
        return;
    }
    m_current_sweep.burst_queue[m_current_sweep.burst_tail] = {};
    m_current_sweep.burst_tail = static_cast<uint8_t>(
        (m_current_sweep.burst_tail + 1U) % kCurrentSweepBurstQueueCount);
    --m_current_sweep.burst_count;
}

bool MotorDiagnostics::flush_current_sweep_active_burst()
{
    auto& runtime = m_current_sweep;
    auto& burst = runtime.active_burst;
    if (burst.sample_count == 0U) {
        return true;
    }
    if (runtime.burst_count >= kCurrentSweepBurstQueueCount) {
        ++runtime.dropped_bursts;
        burst = {};
        return false;
    }
    runtime.burst_queue[runtime.burst_head] = burst;
    runtime.burst_head = static_cast<uint8_t>(
        (runtime.burst_head + 1U) % kCurrentSweepBurstQueueCount);
    ++runtime.burst_count;
    burst = {};
    return true;
}

void MotorDiagnostics::capture_current_sweep_sample_from_isr(
    float feedback_current_a,
    uint32_t frequency_millihz,
    CurrentSweepStage stage)
{
    auto& runtime = m_current_sweep;
    if (!runtime.active || runtime.draining ||
        !algo::motor::basic_math::is_finite_float(feedback_current_a) ||
        (fabsf(feedback_current_a) > kCurrentSweepAbsoluteCurrentLimitA) ||
        ((stage == CurrentSweepStage::Sweep) &&
         (frequency_millihz == 0U))) {
        runtime.safety_fault = true;
        return;
    }

    if ((runtime.stage != stage) ||
        (runtime.current_frequency_millihz != frequency_millihz)) {
        (void)flush_current_sweep_active_burst();
        runtime.stage = stage;
        runtime.current_frequency_millihz = frequency_millihz;
        runtime.frequency_sample_index = 0U;
    }

    auto& burst = runtime.active_burst;
    if (burst.sample_count == 0U) {
        burst = {};
        burst.valid = true;
        burst.version = 2U;
        burst.stage = stage;
        burst.flags = (runtime.frequency_sample_index == 0U) ? 0x01U : 0U;
        burst.session_id = runtime.session_id;
        burst.packet_seq = runtime.next_packet_seq++;
        burst.first_sample_seq = runtime.sample_seq;
        burst.first_frequency_sample_index = runtime.frequency_sample_index;
        burst.frequency_millihz = frequency_millihz;
        burst.q_bias_current_ma = static_cast<int16_t>(
            runtime.config.q_bias_current_a * 1000.0F + 0.5F);
        burst.amplitude_ma = static_cast<uint16_t>(
            runtime.config.amplitude_a * 1000.0F + 0.5F);
    }

    float current_ma = feedback_current_a * 1000.0F;
    current_ma = algo::motor::basic_math::clamp_float(current_ma,
                                               -32768.0F,
                                               32767.0F);
    burst.feedback_current_ma[burst.sample_count] =
        static_cast<int16_t>(current_ma +
                             ((current_ma >= 0.0F) ? 0.5F : -0.5F));
    ++burst.sample_count;
    ++runtime.sample_seq;
    ++runtime.frequency_sample_index;
    if (burst.sample_count >= kCurrentSweepSamplesPerBurst) {
        (void)flush_current_sweep_active_burst();
    }
}

void MotorDiagnostics::finish_current_sweep(uint8_t ret)
{
    {

        if (!m_current_sweep.active || m_current_sweep.draining) {
            return;
        }
        (void)flush_current_sweep_active_burst();
        m_current_sweep.ret = ret;
        m_current_sweep.draining = true;
        m_current_sweep.drain_elapsed_ms = 0U;
        drive_ = {};
        drive_ = {};
    }
    drive_ = {};
}

void MotorDiagnostics::current_sweep_slow_task()
{
    bool active = false;
    bool draining = false;
    bool stop_requested = false;
    bool hardware_fault = false;
    bool generator_complete = false;
    uint32_t total_elapsed_ms = 0U;
    {

        active = m_current_sweep.active;
        if (!active) {
            return;
        }
        draining = m_current_sweep.draining;
        if (!draining && !output_pending()) {
            ++m_current_sweep.total_elapsed_ms;
        }
        total_elapsed_ms = m_current_sweep.total_elapsed_ms;
        stop_requested = m_current_sweep.stop_requested;
        hardware_fault = m_current_sweep.safety_fault ||
                         hardware_fault_ ||
                         hardware_fault_ ||
                         hardware_fault_ ||
                         hardware_fault_ || hardware_fault_;
        generator_complete = m_current_sweep.generator_complete;
    }

    if (draining) {

        ++m_current_sweep.drain_elapsed_ms;
        if ((m_current_sweep.burst_count == 0U) ||
            (m_current_sweep.drain_elapsed_ms >= kCurrentSweepDrainTimeoutMs)) {
            if (m_current_sweep.burst_count != 0U) {
                m_current_sweep.dropped_bursts += m_current_sweep.burst_count;
                m_current_sweep.burst_count = 0U;
            }
            m_current_sweep.active = false;
            m_current_sweep.finished = true;
        }
        return;
    }
    if (stop_requested) {
        finish_current_sweep(kCurrentSweepCancelledRet);
    } else if (hardware_fault) {
        finish_current_sweep(static_cast<uint8_t>(MotorStatus::HardwareError));
    } else if (total_elapsed_ms >= kCurrentSweepRunTimeoutMs) {
        finish_current_sweep(static_cast<uint8_t>(MotorStatus::Timeout));
    } else if (generator_complete) {
        finish_current_sweep(static_cast<uint8_t>(MotorStatus::Ok));
    }
}

void MotorDiagnostics::finish_rl_identification(uint8_t ret)
{
    {

        if (!m_rl_identification.active ||
            (m_rl_identification.stage == RlIdentificationStage::Drain)) {
            return;
        }
        uint8_t final_ret = ret;
        if (!flush_rl_identification_active_burst() &&
            (final_ret == static_cast<uint8_t>(MotorStatus::Ok))) {
            final_ret = static_cast<uint8_t>(MotorStatus::Busy);
        }
        m_rl_identification.ret = final_ret;
        m_rl_identification.stage = RlIdentificationStage::Drain;
        m_rl_identification.drain_elapsed_ms = 0U;
        m_rl_identification.resistance_current_target_a = 0.0F;
        m_rl_identification.resistance_integral_v = 0.0F;
        drive_ = {};
    }
    drive_ = {};
}

void MotorDiagnostics::rl_identification_slow_task()
{
    RlIdentificationStage stage = RlIdentificationStage::Idle;
    bool stop_requested = false;
    bool overflow = false;
    bool safety_fault = false;
    uint32_t total_elapsed_ms = 0U;
    {

        if (!m_rl_identification.active) {
            return;
        }
        stage = m_rl_identification.stage;
        if (stage != RlIdentificationStage::Drain && !output_pending()) {
            ++m_rl_identification.total_elapsed_ms;
        }
        total_elapsed_ms = m_rl_identification.total_elapsed_ms;
        stop_requested = m_rl_identification.stop_requested;
        overflow = m_rl_identification.overflow;
        safety_fault = m_rl_identification.safety_fault;
    }

    if (stage != RlIdentificationStage::Drain) {
    if (stop_requested) {
        finish_rl_identification(kRlIdentificationCancelledRet);
        return;
    }
    if (overflow) {
        finish_rl_identification(static_cast<uint8_t>(MotorStatus::Busy));
        return;
    }
    if (safety_fault || protection_active() || hardware_fault_) {
        finish_rl_identification(static_cast<uint8_t>(MotorStatus::HardwareError));
        return;
    }
    if ((stage != RlIdentificationStage::Drain) &&
        (total_elapsed_ms >= kRlRunTimeoutMs)) {
        finish_rl_identification(static_cast<uint8_t>(MotorStatus::Timeout));
        return;
    }

    }

    if (stage == RlIdentificationStage::Drain) {

        ++m_rl_identification.drain_elapsed_ms;
        if ((m_rl_identification.burst_count == 0U) ||
            (m_rl_identification.drain_elapsed_ms >= kRlDrainTimeoutMs)) {
            if (m_rl_identification.burst_count != 0U) {
                m_rl_identification.dropped_bursts +=
                    m_rl_identification.burst_count;
                m_rl_identification.ret = static_cast<uint8_t>(MotorStatus::Timeout);
                m_rl_identification.burst_count = 0U;
            }
            m_rl_identification.active = false;
            m_rl_identification.finished = true;
            m_rl_identification.stage = RlIdentificationStage::Idle;
        }
        return;
    }

    if (output_pending()) {
        return;
    }

    bool complete = false;
    {

        ++m_rl_identification.stage_elapsed_ms;
        switch (m_rl_identification.stage) {
        case RlIdentificationStage::Prepare:
            if (m_rl_identification.stage_elapsed_ms >= kRlPrepareDurationMs) {
                m_rl_identification.stage = RlIdentificationStage::Resistance;
                m_rl_identification.stage_elapsed_ms = 0U;
                m_rl_identification.sample_seq = 0U;
                m_rl_identification.resistance_current_target_a =
                    kRlResistanceCurrentTargetsA[0];
            }
            break;
        case RlIdentificationStage::Resistance: {
            const uint32_t point = m_rl_identification.stage_elapsed_ms /
                                   kRlResistancePointDurationMs;
            if (point < 4U) {
                m_rl_identification.resistance_current_target_a =
                    kRlResistanceCurrentTargetsA[point];
            }
            if (m_rl_identification.stage_elapsed_ms >=
                    kRlResistanceDurationMs) {
                if (flush_rl_identification_active_burst()) {
                    m_rl_identification.stage =
                        RlIdentificationStage::Inductance;
                    m_rl_identification.stage_elapsed_ms = 0U;
                    m_rl_identification.resistance_current_target_a = 0.0F;
                    m_rl_identification.resistance_integral_v = 0.0F;
                    m_rl_identification.inductance_excitation_samples = 0U;
                }
            }
            break;
        }
        case RlIdentificationStage::Inductance:
            if (m_rl_identification.stage_elapsed_ms ==
                kRlInductanceSettleDurationMs) {
                (void)flush_rl_identification_active_burst();
            }
            if (m_rl_identification.stage_elapsed_ms >=
                    kRlInductanceDurationMs) {
                complete = flush_rl_identification_active_burst();
            }
            break;
        case RlIdentificationStage::Idle:
        case RlIdentificationStage::Drain:
        default:
            break;
        }
    }
    if (complete) {
        finish_rl_identification(static_cast<uint8_t>(MotorStatus::Ok));
    }
}

}  // namespace algo::motor
