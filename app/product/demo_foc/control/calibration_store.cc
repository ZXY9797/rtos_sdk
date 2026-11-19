#include "control/calibration_store.h"
#include <boot_layout.h>
#include <drivers/flash.h>
#include <nvs/nvs.h>

namespace app {
namespace {
#if defined(CONFIG_DEMO_FOC_MD007_V1)
constexpr uint16_t kCalibrationBase = 0x4200U;
constexpr uint16_t kInteractionId = 0x4300U;
#else
constexpr uint16_t kCalibrationBase = 0x4000U;
constexpr uint16_t kInteractionId = 0x4100U;
#endif
hal::Flash& flash_device()
{
    static hal::Flash flash(hal::flash_create_default());
    return flash;
}
nvs::Nvs<hal::Flash>& store()
{
    auto& flash = flash_device();
    static nvs::Nvs<hal::Flash> instance(flash, boot::layout::kStorageOffset,
                                        CONFIG_NVS_SECTOR_COUNT);
    return instance;
}
uint16_t storage_id(CalibrationRecordId id)
{
    return uint16_t(kCalibrationBase + static_cast<unsigned>(id));
}
}

bool CalibrationStore::mount()
{
    if (uint64_t(flash_device().erase_sector_size()) * CONFIG_NVS_SECTOR_COUNT >
        boot::layout::kStorageSize) return false;
    return store().mount() == nvs::Status::Ok;
}

bool CalibrationStore::read(CalibrationRecordId id, void* payload, size_t size)
{
    using Record = algo::motor::CalibrationRecord;
    if (!payload || size > Record::maximum_payload_size) return false;
    const int32_t count = store().read(storage_id(id), record_, sizeof(record_));
    if (count < 0) return false;
    uint32_t sequence = 0U;
    if (!Record::decode(MotorControl::record_type(id), record_, static_cast<size_t>(count),
                        payload, size, &sequence)) return false;
    if (int32_t(sequence - sequence_) > 0) sequence_ = sequence;
    return true;
}

void CalibrationStore::load(CalibrationValues& values)
{
    struct Destination { CalibrationRecordId id; void* data; size_t size; };
    const Destination destinations[] {
        {CalibrationRecordId::CurrentLinear, &values.current, sizeof(values.current)},
        {CalibrationRecordId::BusVoltage, &values.bus, sizeof(values.bus)},
        {CalibrationRecordId::InputTmr, &values.input, sizeof(values.input)},
        {CalibrationRecordId::OutputTmr, &values.output, sizeof(values.output)},
        {CalibrationRecordId::ElectricalAngle, &values.electrical, sizeof(values.electrical)},
        {CalibrationRecordId::JointZero, &values.joint.joint_zero_angle, sizeof(values.joint.joint_zero_angle)},
        {CalibrationRecordId::JointOffset, &values.joint.joint_offset_angle, sizeof(values.joint.joint_offset_angle)},
    };
    for (const auto& destination : destinations)
        (void)read(destination.id, destination.data, destination.size);
}

bool CalibrationStore::write(const RecordView& record)
{
    using Record = algo::motor::CalibrationRecord;
    const auto type = MotorControl::record_type(record.id);
    const size_t size = Record::encode(type, ++sequence_, record.payload, record.size,
                                       record_, sizeof(record_));
    if (!size || store().write(storage_id(record.id), record_, size) != int32_t(size)) return false;
    const int32_t count = store().read(storage_id(record.id), record_, sizeof(record_));
    return count == int32_t(size) &&
        Record::verify_readback(type, record_, size, record.payload, record.size);
}

bool CalibrationStore::erase(CalibrationRecordId id)
{
    if (static_cast<unsigned>(id) > static_cast<unsigned>(CalibrationRecordId::JointOffset))
        return false;
    return store().remove(storage_id(id)) == nvs::Status::Ok &&
        store().read(storage_id(id), record_, sizeof(record_)) == -int32_t(nvs::Status::NotFound);
}
bool CalibrationStore::write_interaction(const uint8_t *bytes, size_t count)
{
    using Record = algo::motor::CalibrationRecord;
    constexpr algo::motor::CalibrationRecordType type{0x494E5443U, 1U};
    const size_t size = Record::encode(type, ++sequence_, bytes, count, record_, sizeof(record_));
    return size && store().write(kInteractionId, record_, size) == int32_t(size) &&
           store().read(kInteractionId, record_, sizeof(record_)) == int32_t(size) &&
           Record::verify_readback(type, record_, size, bytes, count);
}
int CalibrationStore::read_interaction(uint8_t *bytes, size_t capacity)
{
    using Record = algo::motor::CalibrationRecord;
    const int32_t count = store().read(kInteractionId, record_, sizeof(record_));
    if (count == -int32_t(nvs::Status::NotFound))
        return 0;
    if (count < int32_t(Record::header_size) || size_t(count) - Record::header_size > capacity)
        return -1;
    const size_t size = size_t(count) - Record::header_size;
    uint32_t sequence = 0;
    if (!Record::decode({0x494E5443U, 1U}, record_, size_t(count), bytes, size, &sequence))
        return -1;
    if (int32_t(sequence - sequence_) > 0)
        sequence_ = sequence;
    return int(size);
}
} // namespace app
