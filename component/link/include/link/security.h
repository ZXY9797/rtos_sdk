#pragma once

#include "frame.h"

#include <cstddef>
#include <cstdint>

namespace link {

enum class SecurityStatus : uint8_t {
    Ok = 0U,
    AuthenticationFailed,
    BufferTooSmall,
    NotSupported,
};

struct SecurityContext {
    uint8_t sender {};
    uint8_t receiver {};
    uint16_t sequence {};
    uint8_t command_set {};
    uint8_t command_id {};
    AckMode ack_mode {AckMode::No};
    EncMode mode {EncMode::None};
    Priority priority {Priority::Low};
    bool is_ack {false};
};

/**
 * Product-owned authenticated-encryption provider.
 *
 * Implementations must bind every SecurityContext field as authenticated
 * data, use a unique nonce for each distinct protected message, and verify the
 * tag before releasing plaintext. A duplicate request can cause protect() to
 * be called again with the exact same ACK context and plaintext; a provider
 * deriving its nonce from that context must reproduce the exact same payload.
 * Exact request retransmissions are filtered by Router before unprotect(); all
 * other replayed nonces must be rejected.
 * The retransmit bit is deliberately excluded because Router sets it on an
 * otherwise byte-identical retry. Keys must come from protected product
 * storage; common code deliberately provides no fallback key or cipher.
 */
class SecurityProvider {
public:
    virtual ~SecurityProvider() = default;

    [[nodiscard]] virtual SecurityStatus protect(
        const SecurityContext& context,
        const uint8_t* plaintext,
        size_t plaintext_size,
        uint8_t* protected_payload,
        size_t protected_capacity,
        size_t& protected_size) = 0;

    [[nodiscard]] virtual SecurityStatus unprotect(
        const SecurityContext& context,
        const uint8_t* protected_payload,
        size_t protected_size,
        uint8_t* plaintext,
        size_t plaintext_capacity,
        size_t& plaintext_size) = 0;
};

#if defined(CONFIG_LINK_SECURITY)
/**
 * Product link-security factory.
 *
 * The product must implement this in common/link_security.cc. Returning
 * nullptr keeps Router::init() fail-closed.
 */
[[nodiscard]] SecurityProvider* product_security_provider();
#endif

} // namespace link
