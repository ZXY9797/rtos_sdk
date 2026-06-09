#pragma once

#include <cstdint>
#include <cstddef>

namespace ble {

enum class Status : uint8_t {
    Ok,
    Busy,
    Error,
    NotConnected,
    InvalidParam,
    Timeout,
};

struct BdAddr {
    uint8_t addr[6];
    uint8_t addr_type; // 0=public, 1=random
};

struct ConnParam {
    uint16_t interval_min; // in 1.25ms units
    uint16_t interval_max;
    uint16_t slave_latency;
    uint16_t sup_timeout;  // in 10ms units
};

struct SecParam {
    enum Level : uint8_t {
        None,
        Open,
        Mitm,
        SecureConn,
    } level;
    bool bonding;
};

enum class EventId : uint8_t {
    StackInit,
    AdvStarted,
    AdvStopped,
    Connected,
    Disconnected,
    PairRequest,
    PairSuccess,
    PairFailed,
    ConnParamUpdate,
};

struct Event {
    EventId id;
    uint8_t conn_idx;
    uint8_t status; // 0=success
    union {
        BdAddr peer_addr;
        uint8_t disconnect_reason;
    };
};

using EventCallback = void (*)(const Event &evt, void *user_data);

} // namespace ble
