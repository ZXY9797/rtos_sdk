#include <algo/calibration/calibration.h>
#include <algo/calibration/calibration_record.h>
#include <algo/motor/feedback_processor.h>
#include <algo/calibration/lut_session.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <limits>

using namespace algo::motor;

static CalibrationConfig config()
{
    CalibrationConfig value;
    value.motor = {true, 14, 48.0F, 3.5F};
    value.input_tmr = {0, 10};
    value.electrical_angle = {0, 100};
    value.output_tmr = {0, 2};
    return value;
}

static void test_electrical_and_output_calibration()
{
    Calibrator calibration;
    assert(calibration.configure(config()));
    MotorSensorCalibData previous;
    previous.input_sensor_calib.valid = true;
    previous.input_sensor_calib.transform[0] = previous.input_sensor_calib.transform[3] = 1;
    assert(calibration.start(CalibrationKind::ElectricalAngle, 0, previous, true));
    const float range = basic_math::kTwoPi * 14;
    for (unsigned i = 0; i < 112; ++i) {
        CalibrationSample sample;
        sample.valid = true;
        sample.applied_open_loop_angle_rad = calibration.drive().target_electrical_angle_rad;
        sample.raw_electrical_angle_rad = std::fmod(1.0F + sample.applied_open_loop_angle_rad, range);
        calibration.update((i+1)*100U, sample);
    }
    assert(calibration.state() == CalibrationState::PendingPersistence);
    assert(std::fabs(calibration.result().elec_angle_calib.zero_offset_rad - 1) < 1.0e-5F);
    for (float offset : calibration.result().elec_angle_calib.angle_offset_rad)
        assert(std::fabs(offset) < 1.0e-4F);
    assert(!calibration.complete_persistence(false));
    assert(!calibration.result().elec_angle_calib.valid);
    assert(!calibration.drive().energize);
    assert(calibration.start(CalibrationKind::OutputTmr, 0, previous, true));
    const unsigned motions = unsigned(448.0F * 48.0F * 1.2F * (6.0F/16.0F) + 0.5F);
    for (unsigned i = 0; i < motions; ++i) {
        CalibrationSample sample;
        sample.valid = true;
        const float phase = float(i) / float(motions) * 1.2F * basic_math::kTwoPi;
        sample.output_tmr1 = 1.6F + 0.5F * std::sin(phase);
        sample.output_tmr2 = 1.7F + 0.6F * std::cos(phase);
        calibration.update((i+1)*2U, sample);
    }
    assert(calibration.state() == CalibrationState::PendingPersistence);
    assert(calibration.sample_count() == 538U);
    assert(std::fabs(calibration.result().output_tmr1_calib.offset - 1.6F) < 0.001F);
    assert(std::fabs(calibration.result().output_tmr1_calib.amp - 0.5F) < 0.001F);
    assert(calibration.complete_persistence(true));
}

static void test_degenerate_input_and_cancellation()
{
    Calibrator calibration;
    assert(calibration.configure(config()));
    MotorSensorCalibData previous;
    assert(calibration.start(CalibrationKind::InputTmr, 0, previous, true));
    CalibrationSample sample;
    sample.valid = true;
    sample.rotor_tmr1 = sample.rotor_tmr2 = 1.5F;
    for (unsigned i = 0; i < 448; ++i) calibration.update((i+1)*10U, sample);
    assert(calibration.state() == CalibrationState::Invalid);
    assert(!calibration.result().input_sensor_calib.valid && !calibration.drive().energize);
    assert(calibration.start(CalibrationKind::InputTmr, 10, previous, true));
    calibration.cancel();
    assert(calibration.state() == CalibrationState::Cancelled);
    assert(!calibration.drive().energize);
    assert(calibration.start(CalibrationKind::InputTmr, 10, previous, true));
    calibration.update(20, sample, true);
    assert(calibration.state() == CalibrationState::Protection);
}

static void test_auto_calibration_sequence()
{
    AutoCalibration automatic;
    MotorSensorCalibData sensors;
    sensors.input_sensor_calib.valid = true;
    automatic.reset(false);
    assert(automatic.poll(sensors, true, true, true, true) == CalibrationKind::None);
    automatic.reset(true);
    assert(automatic.poll(sensors, true, true, true, false) == CalibrationKind::None);
    // Any missing stage triggers the complete source sequence, including InputTmr.
    assert(automatic.poll(sensors, true, true, true, true) == CalibrationKind::InputTmr);
    assert(automatic.poll(sensors, false, true, true, true) == CalibrationKind::None);
    assert(automatic.failed());
    assert(automatic.poll(sensors, true, true, true, true) == CalibrationKind::None);
    automatic.reset(true);
    assert(automatic.poll(sensors, true, true, true, true) == CalibrationKind::InputTmr);
    assert(automatic.poll(sensors, true, true, true, true) == CalibrationKind::ElectricalAngle);
    assert(automatic.poll(sensors, true, true, true, true) == CalibrationKind::OutputTmr);
    sensors.elec_angle_calib.valid = true;
    sensors.output_tmr1_calib.valid = sensors.output_tmr2_calib.valid = true;
    assert(automatic.poll(sensors, true, true, true, true) == CalibrationKind::None);
    assert(!automatic.active() && !automatic.failed());
}

static void test_storage_record()
{
    assert(CalibrationRecord::crc32("123456789", 9) == 0xCBF43926U);
    const auto type = calibration_records::bus_voltage;
    const MotorLinearCalibData payload {true, 0.25F, 1.02F};
    std::array<uint8_t, 100> record {};
    const size_t count = CalibrationRecord::encode(type, 0xFFFFFFFFU, &payload,
        sizeof(payload), record.data(), record.size());
    assert(count == 28U + sizeof(payload));
    assert(record[0] == 'L' && record[1] == 'A' && record[2] == 'C' && record[3] == 'M');
    MotorLinearCalibData decoded;
    uint32_t sequence = 0;
    assert(CalibrationRecord::decode(type, record.data(), count, &decoded, sizeof(decoded), &sequence));
    assert(sequence == 0xFFFFFFFFU && decoded.offset == payload.offset && decoded.gain == payload.gain);
    assert(CalibrationRecord::verify_readback(type, record.data(), count, &payload, sizeof(payload)));
    record[29] ^= 1U;
    const auto saved = decoded;
    assert(!CalibrationRecord::decode(type, record.data(), count, &decoded, sizeof(decoded)));
    assert(std::memcmp(&saved, &decoded, sizeof(saved)) == 0);
    assert(!CalibrationRecord::decode(type, record.data(), 2, &decoded, sizeof(decoded)));
}

static void test_lut_session()
{
    MotorAngleLutData data;
    for (unsigned i = 0; i < 360; ++i) data.angle_offset_rad[i] = float(i) * 0.0001F;
    const uint32_t crc = CalibrationRecord::crc32(data.angle_offset_rad, sizeof(data.angle_offset_rad));
    AngleLutRequest request;
    request.action = AngleLutAction::Start;
    request.table = AngleLutTable::Output;
    request.session = 10;
    request.count = 360;
    request.crc = crc;
    LutSession session;
    assert(!session.begin(request, 1, 0, false));
    assert(session.begin(request, 1, 0, true));
    AngleLutRequest finish = request;
    finish.action = AngleLutAction::Finish;
    finish.count = 0;
    assert(!session.finish(finish, 1));
    request.action = AngleLutAction::Data;
    request.count = 120;
    request.values = reinterpret_cast<const uint8_t*>(data.angle_offset_rad);
    assert(!session.accept(request, 2, 1));
    assert(session.accept(request, 1, 1));
    assert(session.accept(request, 1, 2));
    float conflicting[120] {};
    conflicting[0] = 1;
    request.values = reinterpret_cast<const uint8_t*>(conflicting);
    assert(!session.accept(request, 1, 3));
    for (unsigned i = 1; i < 3; ++i) {
        request.index = uint16_t(i*120);
        request.values = reinterpret_cast<const uint8_t*>(&data.angle_offset_rad[i*120]);
        assert(session.accept(request, 1, i+3));
    }
    assert(session.finish(finish, 1));
    assert(session.pending_persistence() && !session.apply_output());
    assert(!session.complete_persistence(false));
    assert(!session.apply_output());
    assert(session.finish(finish, 1));
    assert(session.complete_persistence(true) && session.apply_output());
    assert(session.finish(finish, 1) && !session.pending_persistence());
    request.action = AngleLutAction::ReadBegin;
    request.session = 11;
    request.index = request.count = 0;
    auto stored = session.data();
    assert(session.begin_read(request, 1, 0xFFFFFF00U, stored, true));
    assert(session.begin_read(request, 1, 0xFFFFFF00U, stored, true));
    request.action = AngleLutAction::Read;
    request.index = 359;
    request.count = 1;
    const float* value = session.read(request, 1, 0xFFFFFF00U);
    assert(value && *value == data.angle_offset_rad[359]);
    assert(session.expire(uint32_t(0xFFFFFF00U + 10001U)));
    assert(!session.read(request, 1, 0));
}

int main()
{
    test_electrical_and_output_calibration();
    test_degenerate_input_and_cancellation();
    test_auto_calibration_sequence();
    test_storage_record();
    test_lut_session();
}
