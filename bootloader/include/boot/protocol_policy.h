#pragma once

#include <boot/protocol.h>

#include <link/frame.h>

#include <cstdint>

namespace boot_proto {

inline constexpr uint8_t LOADER_ADDRESS = 0x20U;

// The recovery parser intentionally accepts only plaintext frames addressed
// to this loader. Broadcast is discovery-only: state-changing commands must
// always use the loader's unicast address.
inline bool request_envelope_allowed(const link::Frame &frame)
{
    if (frame.cmd_set != CMD_SET || frame.is_ack()
        || frame.enc_mode() != link::EncMode::None
        || !link::is_unicast_addr(frame.sender_id)) {
        return false;
    }

    if (frame.receiver_id == LOADER_ADDRESS) {
        return true;
    }

    return frame.receiver_id == link::ADDR_BROADCAST
        && frame.cmd_id == CMD_QUERY_STATUS;
}

} // namespace boot_proto
