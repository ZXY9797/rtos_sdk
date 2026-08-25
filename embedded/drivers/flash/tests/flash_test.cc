#include <drivers/flash.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",            \
                         __FILE__, __LINE__, #condition);                    \
            std::abort();                                                    \
        }                                                                    \
    } while (false)

namespace {

constexpr uint32_t kBase = 0x1000U;

struct FakeFlash {
    std::array<uint8_t, 64> bytes{};
    unsigned unlock_count {0};
    unsigned lock_count {0};
    unsigned write_count {0};
    bool fail_write {false};
    bool probe_busy {false};
    hal::Flash *active_driver {nullptr};
    hal::Status nested_status {hal::Status::Ok};

    FakeFlash() { bytes.fill(0xFFU); }
};

hal::Status read_cb(void *ctx, uint32_t offset, void *data, size_t len) {
    auto &fake = *static_cast<FakeFlash *>(ctx);
    if (static_cast<uint64_t>(offset) + len > fake.bytes.size()) {
        return hal::Status::InvalidArgument;
    }
    std::memcpy(data, fake.bytes.data() + offset, len);
    return hal::Status::Ok;
}

hal::Status write_cb(void *ctx, uint32_t address,
                     const void *data, size_t len) {
    auto &fake = *static_cast<FakeFlash *>(ctx);
    ++fake.write_count;
    if (fake.probe_busy && fake.active_driver != nullptr) {
        uint8_t byte = 0U;
        fake.nested_status = fake.active_driver->read(0U, &byte, sizeof(byte));
    }
    if (fake.fail_write) return hal::Status::HardwareError;
    if (address < kBase || address - kBase + len > fake.bytes.size()
        || ((address - kBase) % 4U) != 0U || len != 4U) {
        return hal::Status::InvalidArgument;
    }
    const auto *src = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < len; ++i) {
        const size_t index = address - kBase + i;
        if ((fake.bytes[index] & src[i]) != src[i]) {
            return hal::Status::HardwareError;
        }
        fake.bytes[index] &= src[i];
    }
    return hal::Status::Ok;
}

hal::Status erase_cb(void *ctx, uint32_t address) {
    auto &fake = *static_cast<FakeFlash *>(ctx);
    if (address < kBase || address - kBase + 16U > fake.bytes.size()
        || ((address - kBase) % 16U) != 0U) {
        return hal::Status::InvalidArgument;
    }
    std::memset(fake.bytes.data() + address - kBase, 0xFF, 16U);
    return hal::Status::Ok;
}

void unlock_cb(void *ctx) {
    ++static_cast<FakeFlash *>(ctx)->unlock_count;
}

void lock_cb(void *ctx) {
    ++static_cast<FakeFlash *>(ctx)->lock_count;
}

hal::Flash make_flash(FakeFlash &fake) {
    return hal::Flash(kBase, static_cast<uint32_t>(fake.bytes.size()),
                      4U, 16U, &fake, write_cb, erase_cb,
                      lock_cb, unlock_cb, read_cb);
}

} // namespace

int main() {
    FakeFlash fake;
    auto flash = make_flash(fake);
    fake.active_driver = &flash;
    CHECK(flash.init() == hal::Status::Ok);

    const uint8_t data[] = {0xF0U, 0xE1U, 0xD2U, 0xC3U};
    CHECK(flash.write(3U, data, sizeof(data)) == hal::Status::Ok);
    CHECK(fake.write_count == 2U);
    CHECK(fake.bytes[2] == 0xFFU && fake.bytes[7] == 0xFFU);
    CHECK(std::memcmp(fake.bytes.data() + 3U, data, sizeof(data)) == 0);
    CHECK(fake.unlock_count == 1U && fake.lock_count == 1U);

    CHECK(flash.erase(1U, 16U) == hal::Status::InvalidArgument);
    CHECK(flash.erase(0U, 15U) == hal::Status::InvalidArgument);
    CHECK(flash.erase(0U, 16U) == hal::Status::Ok);
    uint8_t boundary = 0U;
    CHECK(flash.read(63U, &boundary, 1U) == hal::Status::Ok);
    CHECK(flash.read(63U, &boundary, 2U) == hal::Status::InvalidArgument);
    CHECK(flash.write(63U, data, 2U) == hal::Status::InvalidArgument);

    fake.probe_busy = true;
    CHECK(flash.write(0U, data, sizeof(data)) == hal::Status::Ok);
    CHECK(fake.nested_status == hal::Status::Busy);
    fake.probe_busy = false;

    fake.fail_write = true;
    const unsigned locks_before = fake.lock_count;
    CHECK(flash.write(0U, data, sizeof(data)) == hal::Status::HardwareError);
    CHECK(fake.lock_count == locks_before + 1U);

    hal::Flash invalid(kBase, static_cast<uint32_t>(fake.bytes.size()),
                       64U, 16U, &fake, write_cb, erase_cb,
                       lock_cb, unlock_cb, read_cb);
    CHECK(invalid.init() == hal::Status::InvalidArgument);

    std::puts("Flash adapter tests passed");
    return 0;
}
