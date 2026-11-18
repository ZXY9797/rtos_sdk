#pragma once
#include <algo/trajectory/s_curve_planner.h>
#include <algo/math/basic_math.h>
#include <algo/motor/status.h>
#include <array>
#include <cstdint>

namespace algo::motor {
enum class BacklashTestState : uint8_t {
    Idle = 0U,
    RampUp,
    Cruise,
    RampDown,
    WaitStopped,
};

// 与 firmware-upgrade 协议的 Cancelled 返回码保持一致。
constexpr uint8_t kBacklashTestCancelledRet = 0x0EU;

struct BacklashTestResultSnapshot {
    bool active {false};
    bool finished {false};
    bool result_valid {false};
    uint8_t ret {0U};
    BacklashTestState state {BacklashTestState::Idle};
    uint8_t speed_index {0U};
    uint8_t completed_cycles {0U};
    uint8_t valid_cycles {0U};
    float result_rad {0.0F};
};


// Single owner. Feed equally filtered input-equivalent and output joint angles.
// The returned velocity is fed to JointController; no hardware is accessed.
class BacklashIdentifier {
    struct BacklashTest {
        struct Config {
            std::array<float, 1U> speeds_rad_s {0.2F};
            uint8_t cycles_per_speed {1U};
            float ramp_time_s {2.0F};
            float cruise_settle_time_s {2.0F};
            float cruise_travel_rad {algo::motor::basic_math::kTwoPi};
            float stopped_wait_time_s {2.0F};
            float total_timeout_s {1000.0F};
        };

        struct Request {
            bool start_requested {false};
            bool stop_requested {false};
            bool emergency_stop_requested {false};
        };

        struct Runtime {
            bool active {false};
            bool cancel_requested {false};
            BacklashTestState state {BacklashTestState::Idle};
            uint8_t speed_index {0U};
            uint8_t cycle_index {0U};
            uint8_t valid_cycles {0U};
            int8_t direction {1};
            uint32_t total_count {0U};
            uint32_t phase_count {0U};
            uint32_t branch_sample_count {0U};
            bool branch_sampling_started {false};
            float command_velocity_rad_s {0.0F};
            float ramp_start_velocity_rad_s {0.0F};
            float branch_start_input_angle_rad {0.0F};
            float branch_error_sum_rad {0.0F};
            float branch_error_compensation_rad {0.0F};
            float branch_mean_rad {0.0F};
            float positive_branch_mean_rad {0.0F};
            float backlash_sum_rad {0.0F};
        };

        Config config {};
        Request request {};
        Runtime runtime {};
        BacklashTestResultSnapshot result {};
    };


public:
    using Config = BacklashTest::Config;
    bool configure(float frequency_hz, const Config& config);
    bool start(bool control_disabled = true);
    void cancel(bool emergency = false);
    float update(float input_equivalent_angle_rad, float output_angle_rad,
                 bool protection_active = false);
    bool active() const { return m_backlash_test.runtime.active; }
    const BacklashTestResultSnapshot& result() const { return m_backlash_test.result; }
private:
    void initialize_backlash_test();
    void begin_backlash_phase(BacklashTestState state);
    void finish_backlash_test(uint8_t ret, bool result_valid);
    bool update_backlash_result_progress();
    void run_backlash_test_step();
    BacklashTest m_backlash_test {};
    float frequency_hz_ {0.0F};
    float input_angle_rad_ {0.0F};
    float output_angle_rad_ {0.0F};
    bool configured_ {false};
};
}  // namespace algo::motor
