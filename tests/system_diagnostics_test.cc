#include <system/diagnostics.h>
#include <system/watchdog.h>
#include <arch/arm/cortex_m/fault.h>
#include <device.h>
#include <osal.h>
#include <cstdlib>
#include <fstream>

#define CHECK(x) do { if (!(x)) std::abort(); } while (false)
namespace osal {
bool Kernel::in_isr() { return false; }
uint32_t Kernel::uptime_ms() { return 123U; }
MemoryStats Kernel::memory_stats() { return {true, 400U, 200U}; }
CriticalSectionGuard::CriticalSectionGuard() = default;
CriticalSectionGuard::~CriticalSectionGuard() = default;
}
namespace hal {
const DeviceInfo* get_device_registry(size_t* count) {
    static const DeviceInfo devices[] = {{1, "test", "test", nullptr,
        [](const void*) { return true; }, nullptr, nullptr, 0U, false, 42U, 3U}};
    *count = 1U; return devices;
}
}
namespace hal::fault { const FaultRecord* getRecord() { return nullptr; } }
namespace boot { bool boot_ctrl_read(uint8_t& flag) { flag = 4U; return true; } }
namespace system_watchdog {
Snapshot snapshot() {
    Snapshot result; result.enabled = true; result.registered_clients = 1U;
    result.clients[0] = {true, false, "control", 100U, 10U}; return result;
}
}
int main(int argc, char** argv)
{
    static runtime::TimingMetrics metrics;
    constexpr runtime::ExecutionSpec spec {"test", runtime::Trigger::Periodic, 100U, 100U, 50U};
    metrics.record(spec, 0U, 10U, 20U);
    CHECK(system_diagnostics::register_timing("test", metrics));
    uint8_t bytes[512] {};
    CHECK(system_diagnostics::encode(bytes, 99U) == 0U);
    const size_t size = system_diagnostics::encode(bytes, sizeof(bytes));
    CHECK(size > 100U && size <= sizeof(bytes) && bytes[0] == 1U);
    // The small buffer contains a complete header and complete TLVs only.
    CHECK(system_diagnostics::encode(bytes, 100U) == 96U);
    const size_t full_size = system_diagnostics::encode(bytes, sizeof(bytes));
    if (argc > 1) {
        std::ofstream file(argv[1], std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes), static_cast<std::streamsize>(full_size));
        CHECK(file.good());
    }
}
