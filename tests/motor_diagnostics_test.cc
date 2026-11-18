#include <algo/calibration/backlash.h>
#include <algo/motor/diagnostics.h>
#include <cassert>
#include <cmath>
#include <limits>

using namespace algo::motor;

static void test_rl_sequence()
{
    MotorDiagnostics diagnostic;
    assert(!diagnostic.start_rl({}));
    assert(diagnostic.start_rl({true, true, true, true}));
    bool resistance = false, positive = false, negative = false;
    unsigned packets = 0;
    for (unsigned ms = 0; ms < 2100 && diagnostic.active(); ++ms) {
        diagnostic.tick_1ms(true);
        for (unsigned n = 0; n < 20; ++n) {
            const auto drive = diagnostic.sample(0.0F, 0.0F, 24.0F);
            if (drive.energize) assert(std::fabs(drive.voltage_v.alpha) <= 1.0F);
        }
        RlIdentificationBurstPacket burst;
        while (diagnostic.peek_rl_identification_burst(1, &burst)) {
            assert(burst.version == 1 && burst.sample_count <= 20 && burst.sample_count > 0);
            assert(burst.first_timestamp_us == burst.first_sample_seq * 50U);
            assert(burst.vbus_mv == 24000U);
            if (burst.stage == RlIdentificationStage::Resistance) resistance = true;
            if (burst.stage == RlIdentificationStage::Inductance) {
                if (burst.applied_voltage_mv == 500) positive = true;
                if (burst.applied_voltage_mv == -500) negative = true;
            }
            auto copy = burst;
            diagnostic.consume_rl_identification_burst(static_cast<uint16_t>(burst.packet_seq+1));
            assert(diagnostic.peek_rl_identification_burst(1, &burst));
            assert(burst.packet_seq == copy.packet_seq);
            diagnostic.consume_rl_identification_burst(burst.packet_seq);
            ++packets;
        }
    }
    assert(!diagnostic.active() && diagnostic.rl_result().finished);
    assert(diagnostic.rl_result().ret == 0U);
    assert(resistance && positive && negative && packets > 1000U);
    assert(diagnostic.dropped_rl_bursts() == 0U);
}

static void test_rl_cancel_overflow_and_brake()
{
    MotorDiagnostics diagnostic;
    assert(diagnostic.start_rl({true, true, true, true}));
    for (unsigned i = 0; i < 200; ++i) diagnostic.tick_1ms(false);
    assert(!diagnostic.sample(0, 0, 24).energize);
    diagnostic.stop();
    diagnostic.tick_1ms(true);
    diagnostic.tick_1ms(true);
    assert(!diagnostic.active() && diagnostic.rl_result().ret == 0x0EU);
    assert(diagnostic.start_rl({true, true, true, true}));
    for (unsigned i = 0; i < 100; ++i) diagnostic.tick_1ms(true);
    for (unsigned i = 0; i < 9U*20U; ++i) diagnostic.sample(0, 0, 24);
    assert(diagnostic.dropped_rl_bursts() == 1U);
    diagnostic.tick_1ms(true);
    RlIdentificationBurstPacket burst;
    while (diagnostic.peek_rl_identification_burst(1, &burst))
        diagnostic.consume_rl_identification_burst(burst.packet_seq);
    diagnostic.tick_1ms(true);
    assert(!diagnostic.active() && diagnostic.rl_result().ret == 3U);
    assert(diagnostic.start_rl({true, true, true, true}));
    assert(!diagnostic.sample(2.01F, 0, 24).energize);
    diagnostic.tick_1ms(true);
    diagnostic.tick_1ms(true);
    assert(!diagnostic.active() && diagnostic.rl_result().ret == 5U);
}

static void test_sweep_sequence_and_loss_policy()
{
    MotorDiagnostics diagnostic;
    CurrentSweepConfig config;
    config.start_frequency_hz = 1000;
    config.end_frequency_hz = 1002;
    config.step_frequency_hz = 2;
    config.repeat_cycles = 1;
    config.preload_duration_ms = 1;
    assert(!diagnostic.start_sweep(config, 10000, {true,true,true,true}));
    assert(diagnostic.start_sweep(config, 20000, {true,true,true,true}));
    bool preload = false, first_frequency = false, last_frequency = false;
    for (unsigned ms = 0; ms < 20 && diagnostic.active(); ++ms) {
        diagnostic.tick_1ms(true);
        for (unsigned i = 0; i < 20; ++i) {
            const auto drive = diagnostic.sample(0, 1.0F, 24);
            if (drive.energize) assert(drive.target_current_a.q >= 0.7F && drive.target_current_a.q <= 1.3F);
        }
        CurrentSweepBurstPacket burst;
        while (diagnostic.peek_current_sweep_burst(1, &burst)) {
            assert(burst.version == 2U && burst.sample_count <= 40U);
            assert(burst.q_bias_current_ma == 1000 && burst.amplitude_ma == 300U);
            for (unsigned i = 0; i < burst.sample_count; ++i) assert(burst.feedback_current_ma[i] == 1000);
            if (burst.stage == CurrentSweepStage::Preload) preload = true;
            if (burst.frequency_millihz == 1000000U) first_frequency = true;
            if (burst.frequency_millihz == 1002000U) last_frequency = true;
            diagnostic.consume_current_sweep_burst(burst.packet_seq);
        }
    }
    assert(preload && first_frequency && last_frequency);
    assert(diagnostic.sweep_result().finished && diagnostic.sweep_result().ret == 0U);
    config = {};
    assert(diagnostic.start_sweep(config, 20000, {true,true,true,true}));
    for (unsigned i = 0; i < 9U*40U; ++i) diagnostic.sample(0, 1, 24);
    assert(diagnostic.dropped_sweep_bursts() == 1U && diagnostic.active());
    diagnostic.stop();
    for (unsigned ms = 0; ms <= 2000; ++ms) diagnostic.tick_1ms(true);
    assert(!diagnostic.active() && diagnostic.sweep_result().ret == 0x0EU);
}

static void test_backlash()
{
    BacklashIdentifier test;
    BacklashIdentifier::Config config;
    config.speeds_rad_s[0] = 1.0F;
    config.ramp_time_s = config.cruise_settle_time_s = config.stopped_wait_time_s = 0.01F;
    config.cruise_travel_rad = 0.01F;
    config.total_timeout_s = 5.0F;
    assert(test.configure(1000, config));
    assert(test.start());
    float input = 0, velocity = 0;
    for (unsigned i = 0; i < 5000 && test.active(); ++i) {
        const float output = input - std::copysign(0.01F, velocity);
        velocity = test.update(input, output);
        input += velocity * 0.001F;
    }
    assert(!test.active() && test.result().result_valid);
    assert(std::fabs(test.result().result_rad - 0.02F) < 1.0e-5F);
    assert(test.result().valid_cycles == 1U);
    assert(test.start());
    test.update(0, 0);
    test.cancel();
    for (unsigned i = 0; i < 100 && test.active(); ++i) test.update(0, 0);
    assert(!test.active() && test.result().ret == 0x0EU);
    assert(test.start());
    test.cancel(true);
    assert(test.update(0, 0) == 0 && !test.active());
    assert(test.start());
    assert(test.update(std::numeric_limits<float>::quiet_NaN(), 0) == 0);
    assert(test.result().ret == 5U);
}

int main()
{
    test_rl_sequence();
    test_rl_cancel_overflow_and_brake();
    test_sweep_sequence_and_loss_policy();
    test_backlash();
}
