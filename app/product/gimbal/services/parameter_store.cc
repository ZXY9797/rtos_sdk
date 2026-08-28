#include "services/parameter_store.h"

#include <boot_layout.h>
#include <drivers/flash.h>
#include <nvs/nvs.h>

#include <cstddef>

namespace app {
namespace {

constexpr uint16_t kFactoryRecordA = 0x3100U;
constexpr uint16_t kFactoryRecordB = 0x3101U;
constexpr uint16_t kUserRecordA = 0x3110U;
constexpr uint16_t kUserRecordB = 0x3111U;

using FactoryRecord =
    gimbal::ParameterRecord<gimbal::FactoryParameters>;
using UserRecord = gimbal::ParameterRecord<gimbal::UserParameters>;

constexpr size_t kNvsV2HeaderSize = 16U;
constexpr size_t kNvsAteSize = 8U;
#if CONFIG_NVS_DATA_CRC
constexpr size_t kNvsDataCrcSize = 4U;
#else
constexpr size_t kNvsDataCrcSize = 0U;
#endif
constexpr size_t kReferenceEraseSectorSize = 0x800U;
constexpr size_t kRecordOverheadBudget = 64U;
static_assert(
    2U * sizeof(FactoryRecord) + kRecordOverheadBudget
        <= kReferenceEraseSectorSize,
    "Gimbal factory A/B live set exceeds one NVS sector");
static_assert(
    2U * sizeof(UserRecord) + kRecordOverheadBudget
        <= kReferenceEraseSectorSize,
    "Gimbal user A/B live set exceeds one NVS sector");

using ParameterNvs = nvs::Nvs<hal::Flash>;

struct RecordLocation {
    ParameterNvs &storage;
    uint16_t id_a;
    uint16_t id_b;
    uint32_t magic;
    uint16_t schema;
};

hal::Flash &storage_flash()
{
    static hal::Flash flash(hal::flash_create_default());
    return flash;
}

[[nodiscard]] uint32_t nvs_span()
{
    return storage_flash().erase_sector_size()
        * static_cast<uint32_t>(CONFIG_NVS_SECTOR_COUNT);
}

[[nodiscard]] bool valid_nvs_span()
{
    const uint64_t span =
        static_cast<uint64_t>(storage_flash().erase_sector_size())
        * static_cast<uint64_t>(CONFIG_NVS_SECTOR_COUNT);
    return span > 0U && 2U * span <= boot::layout::kStorageSize;
}

[[nodiscard]] uint64_t align_up(uint64_t value, uint32_t alignment)
{
    return (value + alignment - 1U) / alignment * alignment;
}

[[nodiscard]] uint64_t nvs_record_footprint(
    size_t record_size, uint32_t write_block_size)
{
    const uint64_t data_size = align_up(
        record_size + kNvsDataCrcSize, write_block_size);
    const uint64_t ate_size = align_up(
        kNvsAteSize, write_block_size) + write_block_size;
    return data_size + ate_size;
}

[[nodiscard]] bool parameter_live_set_fits(size_t record_size)
{
    hal::Flash &flash = storage_flash();
    const uint32_t write_block_size = flash.write_block_size();
    if (write_block_size < 4U || write_block_size > 32U
        || (write_block_size & (write_block_size - 1U)) != 0U) {
        return false;
    }
    const uint64_t header_size = align_up(
        kNvsV2HeaderSize, write_block_size) + write_block_size;
    const uint64_t required = header_size
        + 2U * nvs_record_footprint(record_size, write_block_size);
    return required <= flash.erase_sector_size();
}

ParameterNvs &factory_storage()
{
    hal::Flash &flash = storage_flash();
    static ParameterNvs instance(
        flash, boot::layout::kStorageOffset
            + boot::layout::kStorageSize - 2U * nvs_span());
    return instance;
}

ParameterNvs &user_storage()
{
    hal::Flash &flash = storage_flash();
    static ParameterNvs instance(
        flash, boot::layout::kStorageOffset
            + boot::layout::kStorageSize - nvs_span());
    return instance;
}

template <typename Record>
[[nodiscard]] bool read_record(
    ParameterNvs &storage, uint16_t id, Record &record)
{
    return storage.read(id, record)
        == static_cast<int32_t>(sizeof(Record));
}

[[nodiscard]] bool newer(uint32_t left, uint32_t right)
{
    const uint32_t delta = left - right;
    return delta != 0U && delta < 0x80000000U;
}

template <typename Record, typename Validator>
[[nodiscard]] bool choose_record(
    const RecordLocation &location, Validator validator, Record &selected)
{
    Record first {};
    Record second {};
    const bool first_valid = read_record(
        location.storage, location.id_a, first)
        && gimbal::valid_parameter_record(
            first, location.magic, location.schema)
        && validator(first.payload);
    const bool second_valid = read_record(
        location.storage, location.id_b, second)
        && gimbal::valid_parameter_record(
            second, location.magic, location.schema)
        && validator(second.payload);
    if (!first_valid && !second_valid) {
        return false;
    }
    selected = second_valid
            && (!first_valid
                || newer(second.generation, first.generation))
        ? second : first;
    return true;
}

template <typename Record>
[[nodiscard]] bool write_next(
    const RecordLocation &location, const Record &record)
{
    const uint16_t id = (record.generation & 1U) == 0U
        ? location.id_a : location.id_b;
    if (location.storage.write(id, record)
        != static_cast<int32_t>(sizeof(Record))) {
        return false;
    }
    Record verified {};
    return read_record(location.storage, id, verified)
        && gimbal::valid_parameter_record(
            verified, record.magic, record.schema)
        && verified.generation == record.generation
        && verified.crc32 == record.crc32;
}

} // namespace

bool ParameterStore::initialize()
{
    hal::Flash &flash = storage_flash();
    if (flash.init() != hal::Status::Ok
        || !valid_nvs_span()
        || !parameter_live_set_fits(sizeof(FactoryRecord))
        || !parameter_live_set_fits(sizeof(UserRecord))
        || factory_storage().mount() != nvs::Status::Ok
        || user_storage().mount() != nvs::Status::Ok) {
        initialized_ = false;
        return false;
    }
    initialized_ = true;
    return true;
}

bool ParameterStore::load(gimbal::FactoryParameters &factory,
                          gimbal::UserParameters &user)
{
    if (!initialized_) {
        return false;
    }
    FactoryRecord factory_record {};
    const RecordLocation factory_location {
        factory_storage(), kFactoryRecordA, kFactoryRecordB,
        gimbal::kFactoryParameterMagic,
        gimbal::kFactoryParameterSchema,
    };
    factory_valid_ = choose_record(
        factory_location, gimbal::valid_factory_parameters,
        factory_record);
    if (factory_valid_) {
        factory = factory_record.payload;
        factory_generation_ = factory_record.generation;
    }
    const auto user_validator = [&factory](
        const gimbal::UserParameters &candidate) {
        return gimbal::valid_user_parameters(candidate, factory);
    };
    UserRecord user_record {};
    const RecordLocation user_location {
        user_storage(), kUserRecordA, kUserRecordB,
        gimbal::kUserParameterMagic, gimbal::kUserParameterSchema,
    };
    user_valid_ = factory_valid_ && choose_record(
        user_location, user_validator, user_record);
    if (user_valid_) {
        user = user_record.payload;
        user_generation_ = user_record.generation;
    }
    return factory_valid_;
}

bool ParameterStore::save_factory(
    const gimbal::FactoryParameters &factory)
{
    if (!initialized_ || !gimbal::valid_factory_parameters(factory)) {
        return false;
    }
    const FactoryRecord record = gimbal::make_parameter_record(
        gimbal::kFactoryParameterMagic,
        gimbal::kFactoryParameterSchema,
        factory_generation_ + 1U, factory);
    const RecordLocation location {
        factory_storage(), kFactoryRecordA, kFactoryRecordB,
        gimbal::kFactoryParameterMagic,
        gimbal::kFactoryParameterSchema,
    };
    if (!write_next(location, record)) {
        return false;
    }
    factory_generation_ = record.generation;
    factory_valid_ = true;
    user_valid_ = false;
    return true;
}

bool ParameterStore::save_user(
    const gimbal::UserParameters &user,
    const gimbal::FactoryParameters &factory)
{
    if (!initialized_ || !factory_valid_
        || !gimbal::valid_user_parameters(user, factory)) {
        return false;
    }
    const UserRecord record = gimbal::make_parameter_record(
        gimbal::kUserParameterMagic, gimbal::kUserParameterSchema,
        user_generation_ + 1U, user);
    const RecordLocation location {
        user_storage(), kUserRecordA, kUserRecordB,
        gimbal::kUserParameterMagic, gimbal::kUserParameterSchema,
    };
    if (!write_next(location, record)) {
        return false;
    }
    user_generation_ = record.generation;
    user_valid_ = true;
    return true;
}

} // namespace app
