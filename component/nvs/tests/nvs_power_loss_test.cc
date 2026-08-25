#include <nvs/nvs.h>

#include <drivers/flash.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <vector>

#define assert(condition)                                                     \
    do {                                                                      \
        if (!(condition)) {                                                   \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",             \
                         __FILE__, __LINE__, #condition);                      \
            std::abort();                                                     \
        }                                                                     \
    } while (false)

struct osal_bare_mutex {};

namespace osal {
Mutex::Mutex() : handle_(new osal_bare_mutex {}) {}
Mutex::~Mutex() { delete handle_; }
bool Mutex::create() { return true; }
void Mutex::destroy() {}
int Mutex::lock(Milliseconds) { return 0; }
int Mutex::try_lock() { return 0; }
int Mutex::unlock() { return 0; }
bool Kernel::in_isr() { return false; }
} // namespace osal

namespace {

constexpr uint32_t kSectorSize = 512U;
constexpr uint32_t kSectorCount = 3U;

struct FakeFlash {
    std::array<uint8_t, kSectorSize * kSectorCount> bytes {};
    int fail_write_at {-1};
    size_t partial_failure_bytes {0U};
    int write_count {0};
    uint32_t write_block_size {4U};

    explicit FakeFlash(uint32_t block_size = 4U)
        : write_block_size(block_size)
    {
        bytes.fill(0xFFU);
    }

    static hal::Status read(void* context, uint32_t offset, void* data, size_t len)
    {
        auto& self = *static_cast<FakeFlash*>(context);
        if (offset + len > self.bytes.size()) return hal::Status::InvalidArgument;
        std::memcpy(data, &self.bytes[offset], len);
        return hal::Status::Ok;
    }

    static hal::Status write(void* context, uint32_t address,
                             const void* data, size_t len)
    {
        auto& self = *static_cast<FakeFlash*>(context);
        const bool fail = self.fail_write_at >= 0
            && self.write_count++ == self.fail_write_at;
        if (address + len > self.bytes.size()
            || len != self.write_block_size
            || (address % self.write_block_size) != 0U) {
            return hal::Status::InvalidArgument;
        }
        const auto* source = static_cast<const uint8_t*>(data);
        const size_t bytes_to_program = fail
            ? std::min(self.partial_failure_bytes, len) : len;
        for (size_t i = 0U; i < bytes_to_program; ++i) {
            if ((source[i] & self.bytes[address + i]) != source[i]) {
                return hal::Status::HardwareError;
            }
            self.bytes[address + i] &= source[i];
        }
        return fail ? hal::Status::HardwareError : hal::Status::Ok;
    }

    static hal::Status erase(void* context, uint32_t address)
    {
        auto& self = *static_cast<FakeFlash*>(context);
        if (address % kSectorSize != 0U || address + kSectorSize > self.bytes.size()) {
            return hal::Status::InvalidArgument;
        }
        std::memset(&self.bytes[address], 0xFF, kSectorSize);
        return hal::Status::Ok;
    }

    static void lock(void*) {}

    hal::Flash make_driver()
    {
        return hal::Flash(0U, static_cast<uint32_t>(bytes.size()),
                          write_block_size, kSectorSize, this,
                          write, erase, lock, lock, read);
    }
};

uint8_t legacy_crc8(const uint8_t* data, size_t len)
{
    uint8_t crc = 0xFFU;
    for (size_t i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 0x80U) != 0U
                ? static_cast<uint8_t>((crc << 1U) ^ 0x31U)
                : static_cast<uint8_t>(crc << 1U);
        }
    }
    return crc;
}

uint32_t value_crc32(const uint8_t* data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0U; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            crc = (crc & 1U) != 0U
                ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

void seed_legacy_entry(FakeFlash& flash, uint16_t sector, uint16_t slot,
                       uint16_t id, uint16_t data_offset,
                       const uint8_t* data, uint16_t len)
{
    const uint32_t base = static_cast<uint32_t>(sector) * kSectorSize;
    std::memcpy(&flash.bytes[base + data_offset], data, len);
    const uint32_t crc = value_crc32(data, len);
    std::memcpy(&flash.bytes[base + data_offset + len], &crc, sizeof(crc));

    nvs::Ate ate {};
    ate.id = id;
    ate.offset = data_offset;
    ate.len = len;
    ate.part = 0xFFU;
    ate.crc8 = legacy_crc8(
        reinterpret_cast<const uint8_t*>(&ate), sizeof(ate) - 1U);
    const uint32_t ate_address = base + kSectorSize
        - (static_cast<uint32_t>(slot) + 1U) * sizeof(ate);
    std::memcpy(&flash.bytes[ate_address], &ate, sizeof(ate));
}

void basic_and_crc_test()
{
    FakeFlash storage;
    auto flash = storage.make_driver();
    assert(flash.init() == hal::Status::Ok);
    nvs::Nvs<hal::Flash> store(flash, 0U, kSectorCount);
    assert(store.mount() == nvs::Status::Ok);

    std::array<uint8_t, 180U> value {};
    for (size_t i = 0U; i < value.size(); ++i) value[i] = static_cast<uint8_t>(i);
    assert(store.write(7U, value.data(), value.size()) ==
           static_cast<int32_t>(value.size()));
    const int32_t free_before = store.available_space();
    assert(store.write(7U, value.data(), value.size()) == 0);
    assert(store.available_space() == free_before);

    std::array<uint8_t, 180U> readback {};
    assert(store.read(7U, readback.data(), readback.size()) ==
           static_cast<int32_t>(readback.size()));
    assert(readback == value);

    storage.bytes[20U + 4U] ^= 0x01U;
    assert(store.read(7U, readback.data(), readback.size()) ==
           -static_cast<int32_t>(nvs::Status::CrcError));
}

void gc_and_remount_test()
{
    FakeFlash storage;
    auto flash = storage.make_driver();
    assert(flash.init() == hal::Status::Ok);
    {
        nvs::Nvs<hal::Flash> store(flash, 0U, kSectorCount);
        assert(store.mount() == nvs::Status::Ok);
        for (uint32_t generation = 0U; generation < 20U; ++generation) {
            std::array<uint8_t, 80U> value {};
            value.fill(static_cast<uint8_t>(generation));
            assert(store.write(10U, value.data(), value.size()) >= 0);
            value.fill(static_cast<uint8_t>(generation + 1U));
            assert(store.write(11U, value.data(), value.size()) >= 0);
        }
        assert(store.remove(11U) == nvs::Status::Ok);
    }

    nvs::Nvs<hal::Flash> remounted(flash, 0U, kSectorCount);
    assert(remounted.mount() == nvs::Status::Ok);
    std::array<uint8_t, 80U> value {};
    assert(remounted.read(10U, value.data(), value.size()) ==
           static_cast<int32_t>(value.size()));
    for (uint8_t byte : value) assert(byte == 19U);
    assert(remounted.read(11U, value.data(), value.size()) ==
           -static_cast<int32_t>(nvs::Status::NotFound));
}

void power_loss_sweep_test()
{
    FakeFlash seeded;
    auto seeded_flash = seeded.make_driver();
    assert(seeded_flash.init() == hal::Status::Ok);
    nvs::Nvs<hal::Flash> seed_store(seeded_flash, 0U, kSectorCount);
    assert(seed_store.mount() == nvs::Status::Ok);
    std::array<uint8_t, 100U> old_value {};
    old_value.fill(0x11U);
    assert(seed_store.write(1U, old_value.data(), old_value.size()) > 0);
    for (uint16_t id = 2U; id < 6U; ++id) {
        std::array<uint8_t, 60U> filler {};
        filler.fill(static_cast<uint8_t>(id));
        assert(seed_store.write(id, filler.data(), filler.size()) > 0);
    }

    const auto snapshot = seeded.bytes;
    std::array<uint8_t, 100U> new_value {};
    new_value.fill(0x77U);
    for (int failure = 0; failure < 180; ++failure) {
        FakeFlash trial;
        trial.bytes = snapshot;
        trial.fail_write_at = failure;
        auto flash = trial.make_driver();
        assert(flash.init() == hal::Status::Ok);
        {
            nvs::Nvs<hal::Flash> store(flash, 0U, kSectorCount);
            assert(store.mount() == nvs::Status::Ok);
            (void)store.write(1U, new_value.data(), new_value.size());
        }
        trial.fail_write_at = -1;
        nvs::Nvs<hal::Flash> recovered(flash, 0U, kSectorCount);
        assert(recovered.mount() == nvs::Status::Ok);
        std::array<uint8_t, 100U> result {};
        assert(recovered.read(1U, result.data(), result.size()) ==
               static_cast<int32_t>(result.size()));
        assert(result == old_value || result == new_value);
    }
}

void wide_write_block_test()
{
    FakeFlash storage(8U);
    auto flash = storage.make_driver();
    assert(flash.init() == hal::Status::Ok);
    {
        nvs::Nvs<hal::Flash> store(flash, 0U, kSectorCount);
        assert(store.mount() == nvs::Status::Ok);
        for (uint16_t id = 1U; id <= 6U; ++id) {
            std::array<uint8_t, 37U> value {};
            value.fill(static_cast<uint8_t>(id));
            assert(store.write(id, value.data(), value.size()) >= 0);
        }
    }
    nvs::Nvs<hal::Flash> recovered(flash, 0U, kSectorCount);
    assert(recovered.mount() == nvs::Status::Ok);
    std::array<uint8_t, 37U> value {};
    assert(recovered.read(6U, value.data(), value.size()) ==
           static_cast<int32_t>(value.size()));
    for (uint8_t byte : value) assert(byte == 6U);
}

void partial_program_recovery_test()
{
    FakeFlash seeded(8U);
    auto seeded_flash = seeded.make_driver();
    assert(seeded_flash.init() == hal::Status::Ok);
    nvs::Nvs<hal::Flash> seed_store(seeded_flash, 0U, kSectorCount);
    assert(seed_store.mount() == nvs::Status::Ok);
    std::array<uint8_t, 72U> old_value {};
    old_value.fill(0x31U);
    assert(seed_store.write(3U, old_value.data(), old_value.size()) > 0);

    const auto snapshot = seeded.bytes;
    std::array<uint8_t, 72U> new_value {};
    new_value.fill(0x92U);
    for (int failure = 0; failure < 80; ++failure) {
        FakeFlash trial(8U);
        trial.bytes = snapshot;
        trial.fail_write_at = failure;
        trial.partial_failure_bytes = 3U;
        auto flash = trial.make_driver();
        assert(flash.init() == hal::Status::Ok);
        {
            nvs::Nvs<hal::Flash> store(flash, 0U, kSectorCount);
            assert(store.mount() == nvs::Status::Ok);
            (void)store.write(3U, new_value.data(), new_value.size());
        }
        trial.fail_write_at = -1;
        nvs::Nvs<hal::Flash> recovered(flash, 0U, kSectorCount);
        assert(recovered.mount() == nvs::Status::Ok);
        std::array<uint8_t, 72U> result {};
        assert(recovered.read(3U, result.data(), result.size()) ==
               static_cast<int32_t>(result.size()));
        assert(result == old_value || result == new_value);
    }
}

void legacy_migration_test()
{
    FakeFlash storage;
    const std::array<uint8_t, 12U> old_value {
        1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U, 10U, 11U, 12U,
    };
    const std::array<uint8_t, 12U> new_value {
        12U, 11U, 10U, 9U, 8U, 7U, 6U, 5U, 4U, 3U, 2U, 1U,
    };
    seed_legacy_entry(storage, 0U, 0U, 42U, 0U,
                      old_value.data(), old_value.size());
    seed_legacy_entry(storage, 0U, 1U, 42U, 32U,
                      new_value.data(), new_value.size());

    auto flash = storage.make_driver();
    assert(flash.init() == hal::Status::Ok);
    {
        nvs::Nvs<hal::Flash> migrated(flash, 0U, kSectorCount);
        assert(migrated.mount() == nvs::Status::Ok);
        std::array<uint8_t, 12U> value {};
        assert(migrated.read(42U, value.data(), value.size()) ==
               static_cast<int32_t>(value.size()));
        assert(value == new_value);
    }
    nvs::Nvs<hal::Flash> remounted(flash, 0U, kSectorCount);
    assert(remounted.mount() == nvs::Status::Ok);
    std::array<uint8_t, 12U> value {};
    assert(remounted.read(42U, value.data(), value.size()) ==
           static_cast<int32_t>(value.size()));
    assert(value == new_value);
}

} // namespace

int main()
{
    basic_and_crc_test();
    gc_and_remount_test();
    power_loss_sweep_test();
    wide_write_block_test();
    partial_program_recovery_test();
    legacy_migration_test();
    std::puts("NVS power-loss tests passed");
    return 0;
}
