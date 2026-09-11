#pragma once

#include <cstddef>
#include <cstdint>

#ifndef CONFIG_LINK_MAX_LINKS
#define CONFIG_LINK_MAX_LINKS 8
#endif

namespace link {

inline constexpr uint8_t DIAGNOSTICS_WIRE_VERSION = 1U;
inline constexpr uint8_t DIAGNOSTICS_STATS_COMMAND = 1U;
inline constexpr uint8_t DIAGNOSTICS_FLAG_SECURITY = 0x01U;
inline constexpr uint8_t DIAGNOSTICS_FLAG_SECURE_ONLY = 0x02U;

struct DiagnosticCounters {
    uint32_t received_frames {};
    uint32_t codec_errors {};
    uint32_t forwarded_frames {};
    uint32_t transmitted_frames {};
    uint32_t security_drops {};
    uint32_t duplicate_requests {};
    uint32_t replay_conflicts {};
    uint32_t ack_timeouts {};
    uint32_t queued_requests {};
    uint32_t completed_requests {};
    uint32_t deferred_requests {};
    uint32_t queue_overflows {};
    uint32_t async_timeouts {};
    uint32_t unhandled_requests {};
    uint32_t transmit_failures {};
};

struct LinkDiagnosticCounters {
    uint16_t link_id {};
    bool is_ready {false};
    uint8_t process_divider {1U};
    uint32_t received_bytes {};
    uint32_t received_frames {};
    uint32_t codec_errors {};
    uint32_t transmitted_frames {};
    uint32_t transmit_failures {};
};

struct DiagnosticSnapshot {
    uint8_t flags {};
    uint16_t high_queue_depth {};
    uint16_t low_queue_depth {};
    uint16_t async_active {};
    DiagnosticCounters counters {};
    LinkDiagnosticCounters links[CONFIG_LINK_MAX_LINKS] {};
    size_t link_count {};
};

/**
 * Encodes the versioned diagnostics payload in little-endian wire order.
 * The encoder emits as many complete link records as fit in the destination.
 */
[[nodiscard]] size_t encode_diagnostics(
    const DiagnosticSnapshot& snapshot,
    uint8_t* destination,
    size_t destination_size);

} // namespace link
