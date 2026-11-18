#pragma once

#include <algo/motor.h>
#include <algo/interaction.h>

namespace app {

enum class ModeAction : uint8_t { Target, Hold, Restore, Disable, SelectMode };

struct ModeCommand {
    ModeAction action {};
    algo::motor::JointTargetMode mode {};
    bool enabled {false};
    uint32_t sequence {0U};
    algo::motor::JointTargetOrder target {};
    algo::interaction::JointTarget physical {};
    // World XYZ + rotation vector XYZ, velocity XYZ/XYZ, and desired wrench.
    float task_pose[6] {}, task_velocity[6] {}, task_wrench[6] {};
};

struct ModeStatus {
    algo::motor::JointTargetMode mode {};
    uint32_t accepted_sequence {0U}, applied_sequence {0U}, faults {0U};
    bool enabled {false}, holding {false}, busy {false}, locally_disabled {false};
};

struct CycleTiming {
    uint32_t count {0U}, last_us {0U}, maximum_us {0U}, overruns {0U};
    uint32_t maximum_interval_us {0U}, previous_start_us {0U};
    void record(uint32_t start_us, uint32_t end_us, uint32_t budget_us)
    {
        const uint32_t elapsed = end_us - start_us;
        if (count) {
            const uint32_t interval = start_us - previous_start_us;
            if (interval > maximum_interval_us) maximum_interval_us = interval;
        }
        previous_start_us = start_us;
        ++count;
        last_us = elapsed;
        if (elapsed > maximum_us) maximum_us = elapsed;
        if (elapsed > budget_us) ++overruns;
    }
};

struct EncoderSample {
    uint32_t sequence {0U}, processed_us {0U}, raw_count {0U};
    bool valid {false};
};

// Timestamps describe software observation, never the encoder's sampling time.
struct CalibrationPair {
    uint32_t sequence {0U}, joint_sequence {0U}, joint_us {0U};
    uint32_t input_us {0U}, encoder_sequence {0U}, encoder_processed_us {0U};
    uint32_t encoder_raw_count {0U}, dropped {0U};
    float input_angle_rad {0.0F}, output_raw_rad {0.0F}, output_calibrated_rad {0.0F};
    uint8_t flags {0U}; // bit0 encoder valid, bit1 new encoder, bit2 input valid
};

struct MotorObservation {
    algo::motor::MotorElecInfoFeedback electrical {};
    algo::motor::JointLoopTrace joint {};
    algo::motor::TemperatureSample temperature {};
    float rotor_tmr[2] {}, output_tmr[2] {};
    float input_angle_rad {0.0F}, electrical_angle_rad {0.0F}, output_raw_rad {0.0F};
    float position_rad {0.0F}, velocity_rad_s {0.0F}, motor_velocity_rad_s {0.0F};
    float q_target_a {0.0F}, q_feedback_a {0.0F};
    algo::motor::foc::Dq current_a {}, voltage_v {};
    uint32_t fast_sequence {0U}, joint_sequence {0U}, input_us {0U}, joint_us {0U};
    CycleTiming timing[3] {}; // current IRQ, joint IRQ, slow service
};

} // namespace app
