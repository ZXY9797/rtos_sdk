#pragma once
#include <runtime/execution.h>
#include <cstddef>
#include <cstdint>

namespace system_diagnostics {
// Registered metrics/names have static lifetime. Register before owners start.
[[nodiscard]] bool register_timing(const char* name, const runtime::TimingMetrics& metrics);
// Task-context, little-endian, versioned complete TLV records only.
[[nodiscard]] size_t encode(uint8_t* destination, size_t capacity);
const char* build_id();
}
