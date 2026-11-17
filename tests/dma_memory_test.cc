#include <drivers/dma_memory.h>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) std::abort(); } while (false)
namespace {
unsigned prepared = 0U, completed = 0U;
bool fail_rx_prepare = false, fail_rx_complete = false;
}
namespace hal {
Status dma_prepare(const void*, size_t, DmaAccess access) {
    ++prepared;
    return fail_rx_prepare && access == DmaAccess::DeviceWrites
        ? Status::InvalidArgument : Status::Ok;
}
Status dma_complete(const void*, size_t, DmaAccess access) {
    ++completed;
    return fail_rx_complete && access == DmaAccess::DeviceWrites
        ? Status::HardwareError : Status::Ok;
}
}
int main() {
    uint8_t tx[8] {}, rx[8] {};
    hal::DmaBufferLease lease;
    CHECK(lease.prepare(nullptr, 8U, rx, 8U) == hal::Status::InvalidArgument);
    CHECK(prepared == 0U && completed == 0U);
    fail_rx_prepare = true;
    CHECK(lease.prepare(tx, 8U, rx, 8U) == hal::Status::InvalidArgument);
    CHECK(prepared == 2U && completed == 1U); // Roll back TX ownership.
    fail_rx_prepare = false;
    CHECK(lease.prepare(tx, 8U, rx, 8U) == hal::Status::Ok);
    CHECK(lease.prepare(tx, 8U, rx, 8U) == hal::Status::Busy);
    fail_rx_complete = true;
    CHECK(lease.finish() == hal::Status::HardwareError);
    CHECK(completed == 3U); // RX error still releases TX exactly once.
    CHECK(lease.finish() == hal::Status::Ok && completed == 3U);
    fail_rx_complete = false;
    {
        hal::DmaBufferLease cancelled;
        CHECK(cancelled.prepare(tx, 8U, rx, 8U) == hal::Status::Ok);
    }
    CHECK(completed == 5U);
}
