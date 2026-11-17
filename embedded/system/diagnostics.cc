#include <system/diagnostics.h>
#include <system/watchdog.h>
#include <arch/arm/cortex_m/fault.h>
#include <device.h>
#include <build_info.h>
#include <runtime/wire.h>
#include <osal.h>
#include <boot/boot_ctrl.h>

namespace system_diagnostics {
namespace {
struct TimingEntry { const char* name; const runtime::TimingMetrics* metrics; };
TimingEntry entries[8] {};
}

const char* build_id() { return sdk_build::id; }

bool register_timing(const char* name, const runtime::TimingMetrics& metrics)
{
    if (name == nullptr) return false;
    osal::CriticalSectionGuard guard;
    for (const TimingEntry& entry : entries) if (entry.metrics == &metrics) return false;
    for (TimingEntry& entry : entries) {
        if (entry.metrics == nullptr) { entry = {name, &metrics}; return true; }
    }
    return false;
}

size_t encode(uint8_t* destination, size_t capacity)
{
    if (capacity < 100U || destination == nullptr || osal::Kernel::in_isr()) return 0U;
    runtime::WireWriter writer(destination, capacity);
    writer.u8(1U);
    writer.text(build_id(), 64U);
    writer.u32(osal::Kernel::uptime_ms());
    const auto memory = osal::Kernel::memory_stats();
    writer.u8(memory.available ? 1U : 0U);
    writer.u32(static_cast<uint32_t>(memory.free_bytes));
    writer.u32(static_cast<uint32_t>(memory.minimum_free_bytes));
    const auto* fault = hal::fault::getRecord();
    const bool valid_fault = fault != nullptr && fault->valid();
    writer.u8(valid_fault ? 1U : 0U);
    writer.u32(valid_fault ? static_cast<uint32_t>(fault->reason) : 0U);
    writer.u32(valid_fault ? fault->frame.pc : 0U);
    size_t count = 0U;
    const auto* devices = hal::get_device_registry(&count);
    writer.u16(static_cast<uint16_t>(count));
    const auto health = system_watchdog::snapshot();
    writer.u8(health.registered_clients);
    if (valid_fault && writer.remaining() >= 44U) {
        uint8_t record[41] {};
        runtime::WireWriter payload(record, sizeof(record));
        payload.text(fault->build_id, 32U);
        payload.u32(static_cast<uint32_t>(fault->detail)); payload.u32(fault->contextHash);
        writer.u8(4U); writer.u16(static_cast<uint16_t>(payload.used()));
        for (size_t i = 0U; i < payload.used(); ++i) writer.u8(record[i]);
    }
    if (writer.remaining() >= 5U) {
        uint8_t boot_flag = 0U;
        const bool boot_valid = boot::boot_ctrl_read(boot_flag);
        writer.u8(5U); writer.u16(2U); writer.u8(boot_valid ? 1U : 0U); writer.u8(boot_flag);
    }
    for (size_t i = 0U; i < count && writer.remaining() >= 12U; ++i) {
        writer.u8(1U); writer.u16(9U);
        writer.u32(devices[i].stable_id); writer.u32(devices[i].capabilities);
        writer.u8(devices[i].is_ready(devices[i].instance) ? 1U : 0U);
    }
    for (const auto& client : health.clients) {
        if (!client.registered || writer.remaining() < 30U) continue;
        uint8_t record[32] {};
        runtime::WireWriter payload(record, sizeof(record));
        payload.text(client.name, 16U);
        payload.u32(client.silence_ms); payload.u32(client.max_silence_ms);
        payload.u8(client.overdue ? 1U : 0U);
        writer.u8(2U); writer.u16(static_cast<uint16_t>(payload.used()));
        for (size_t i = 0U; i < payload.used(); ++i) writer.u8(record[i]);
    }
    for (size_t index = 0U; index < 8U; ++index) {
        TimingEntry entry {};
        { osal::CriticalSectionGuard guard; entry = entries[index]; }
        if (entry.metrics == nullptr || writer.remaining() < 72U) continue;
        const auto timing = entry.metrics->snapshot();
        uint8_t record[72] {};
        runtime::WireWriter payload(record, sizeof(record));
        payload.text(entry.name, 16U);
        payload.u32(timing.count); payload.u32(timing.max_execution_us);
        payload.u32(timing.max_latency_us); payload.u32(timing.budget_misses);
        payload.u32(timing.deadline_misses);
        for (uint32_t n : timing.latency_histogram) payload.u32(n);
        writer.u8(3U); writer.u16(static_cast<uint16_t>(payload.used()));
        for (size_t i = 0U; i < payload.used(); ++i) writer.u8(record[i]);
    }
    return writer.used();
}
}
