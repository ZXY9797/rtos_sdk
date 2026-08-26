/**
 * BleStack implementation for Goodix GR5525.
 *
 * Wraps ble_stack_init(), GAP parameters, advertising, and connection management.
 * Translates GR5525 BLE events into vendor-neutral ble::Event.
 */

#include "ble/ble_stack.h"
#include "goodix_status.h"

#include <irq.h>
#include <osal.h>

extern "C" {
#include "ble.h"
#include "platform_sdk.h"
#include "app_log.h"
}

#include "ble_cfg.h"

#include <atomic>
#include <cstring>

/* Stub for BLE SDK internal logging — not provided by the SDK library */
extern "C" void stack_raw_log_output(const char *fmt, ...) { (void)fmt; }

namespace ble {

/* ---- GR5525 BLE heap (must be global, allocated before ble_stack_init) ---- */
static STACK_HEAP_INIT(s_heaps_table);

/* ---- Static state (ISR/callback-safe atomics for cross-context access) ---- */
static std::atomic<EventCallback> s_event_cb {nullptr};
static std::atomic<void *> s_event_user_data {nullptr};
static std::atomic<bool> s_is_connected {false};
static std::atomic<uint8_t> s_conn_idx {0U};
static std::atomic<uint32_t> s_peer_sequence {0U};
static std::atomic<uint8_t> s_peer_addr[6] {};
static std::atomic<uint8_t> s_peer_addr_type {0U};
static std::atomic<bool> s_stack_started {false};
static StackConfig s_cfg{};
static char s_device_name[249U] {};
static uint8_t s_adv_data[31U] {};
static uint8_t s_scan_rsp_data[31U] {};
static size_t s_adv_data_length {0U};
static size_t s_scan_rsp_data_length {0U};
static bool s_adv_data_configured {false};
static bool s_scan_rsp_data_configured {false};

static sdk_err_t configure_gap_and_security()
{
    const size_t name_length = std::strlen(s_cfg.device_name);
    sdk_err_t error = ble_gap_device_name_set(
        BLE_GAP_WRITE_PERM_DISABLE,
        reinterpret_cast<const uint8_t *>(s_cfg.device_name),
        static_cast<uint16_t>(name_length));
    if (error != SDK_SUCCESS) {
        return error;
    }

    ble_gap_conn_param_t conn_param {};
    conn_param.interval_min = s_cfg.conn_param.interval_min;
    conn_param.interval_max = s_cfg.conn_param.interval_max;
    conn_param.slave_latency = s_cfg.conn_param.slave_latency;
    conn_param.sup_timeout = s_cfg.conn_param.sup_timeout;
    error = ble_gap_ppcp_set(&conn_param);
    if (error != SDK_SUCCESS) {
        return error;
    }

    ble_sec_param_t sec_param {};
    switch (s_cfg.sec_param.level) {
    case SecParam::Level::None:
    case SecParam::Level::Open:
        sec_param.level = BLE_SEC_MODE1_LEVEL1;
        break;
    case SecParam::Level::Mitm:
        sec_param.level = BLE_SEC_MODE1_LEVEL2;
        break;
    case SecParam::Level::SecureConn:
        sec_param.level = BLE_SEC_MODE1_LEVEL3;
        break;
    default:
        return SDK_ERR_INVALID_PARAM;
    }
    sec_param.auth = s_cfg.sec_param.bonding
        ? BLE_SEC_AUTH_BOND : BLE_SEC_AUTH_NONE;
    sec_param.io_cap = BLE_SEC_IO_NO_INPUT_NO_OUTPUT;
    sec_param.key_size = 16U;
    sec_param.ikey_dist = BLE_SEC_KDIST_ENCKEY | BLE_SEC_KDIST_IDKEY;
    sec_param.rkey_dist = BLE_SEC_KDIST_ENCKEY | BLE_SEC_KDIST_IDKEY;
    error = ble_sec_params_set(&sec_param);
    if (error != SDK_SUCCESS) {
        return error;
    }
    return ble_gap_privacy_params_set(150U, true);
}

/* ---- GR5525 event → ble::Event translation ---- */
static void gr5525_evt_handler(const ble_evt_t *p_evt) {
    if (p_evt == nullptr) {
        return;
    }
    Event evt{};
    evt.status = static_cast<uint8_t>(p_evt->evt_status);

    switch (p_evt->evt_id) {
    case BLE_COMMON_EVT_STACK_INIT:
        evt.id = EventId::StackInit;
        if (p_evt->evt_status == BLE_SUCCESS) {
            evt.status = static_cast<uint8_t>(configure_gap_and_security());
        }
        break;

    case BLE_GAPM_EVT_ADV_START:
        evt.id = (p_evt->evt_status == BLE_SUCCESS) ? EventId::AdvStarted : EventId::AdvStopped;
        break;

    case BLE_GAPM_EVT_ADV_STOP:
        evt.id = EventId::AdvStopped;
        break;

    case BLE_GAPC_EVT_CONNECTED:
        evt.id = EventId::Connected;
        evt.conn_idx = p_evt->evt.gapc_evt.index;
        s_is_connected.store(true, std::memory_order_release);
        s_conn_idx.store(evt.conn_idx, std::memory_order_release);
        s_peer_sequence.fetch_add(1U, std::memory_order_acq_rel);
        std::memcpy(evt.peer_addr.addr,
                    p_evt->evt.gapc_evt.params.connected.peer_addr.addr, 6U);
        evt.peer_addr.addr_type =
            p_evt->evt.gapc_evt.params.connected.peer_addr_type;
        for (size_t i = 0U; i < 6U; ++i) {
            s_peer_addr[i].store(evt.peer_addr.addr[i],
                                 std::memory_order_relaxed);
        }
        s_peer_addr_type.store(evt.peer_addr.addr_type,
                               std::memory_order_relaxed);
        s_peer_sequence.fetch_add(1U, std::memory_order_release);
        break;

    case BLE_GAPC_EVT_DISCONNECTED:
        evt.id = EventId::Disconnected;
        evt.conn_idx = p_evt->evt.gapc_evt.index;
        evt.disconnect_reason = p_evt->evt.gapc_evt.params.disconnected.reason;
        s_is_connected.store(false, std::memory_order_release);
        break;

    case BLE_GAPC_EVT_CONN_PARAM_UPDATE_REQ:
        evt.status = static_cast<uint8_t>(ble_gap_conn_param_update_reply(
            p_evt->evt.gapc_evt.index, true));
        evt.id = EventId::ConnParamUpdate;
        evt.conn_idx = p_evt->evt.gapc_evt.index;
        break;

    case BLE_SEC_EVT_LINK_ENC_REQUEST: {
        evt.id = EventId::PairRequest;
        evt.conn_idx = p_evt->evt.sec_evt.index;
        // Auto-accept pairing
        ble_sec_cfm_enc_t cfm{};
        cfm.req_type = BLE_SEC_PAIR_REQ;
        cfm.accept = true;
        evt.status = static_cast<uint8_t>(
            ble_sec_enc_cfm(p_evt->evt.sec_evt.index, &cfm));
        break;
    }

    case BLE_SEC_EVT_LINK_ENCRYPTED:
        evt.id = (p_evt->evt_status == BLE_SUCCESS) ? EventId::PairSuccess : EventId::PairFailed;
        evt.conn_idx = p_evt->evt.sec_evt.index;
        break;

    default:
        return; // Don't dispatch unhandled events
    }

    const EventCallback callback = s_event_cb.load(std::memory_order_acquire);
    if (callback != nullptr) {
        callback(evt, s_event_user_data.load(std::memory_order_acquire));
    }
}

/* ---- BleStack implementation ---- */

Status BleStack::init(const StackConfig &cfg, EventCallback cb, void *user_data) {
    if (cb == nullptr || cfg.device_name == nullptr
        || cfg.adv_interval_min == 0U
        || cfg.adv_interval_min > cfg.adv_interval_max
        || (cfg.adv_data == nullptr && cfg.adv_data_len != 0U)
        || cfg.adv_data_len > sizeof(s_adv_data)
        || (cfg.scan_rsp_data == nullptr && cfg.scan_rsp_data_len != 0U)
        || cfg.scan_rsp_data_len > sizeof(s_scan_rsp_data)) {
        return Status::InvalidParam;
    }
    size_t name_length = 0U;
    while (name_length <= 248U && cfg.device_name[name_length] != '\0') {
        ++name_length;
    }
    if (name_length == 0U || name_length > 248U) {
        return Status::InvalidParam;
    }
    bool expected = false;
    if (!s_stack_started.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return Status::Busy;
    }

    std::memcpy(s_device_name, cfg.device_name, name_length);
    s_device_name[name_length] = '\0';
    if (!s_adv_data_configured && cfg.adv_data_len > 0U) {
        std::memcpy(s_adv_data, cfg.adv_data, cfg.adv_data_len);
        s_adv_data_length = cfg.adv_data_len;
    }
    if (!s_scan_rsp_data_configured && cfg.scan_rsp_data_len > 0U) {
        std::memcpy(s_scan_rsp_data, cfg.scan_rsp_data,
                    cfg.scan_rsp_data_len);
        s_scan_rsp_data_length = cfg.scan_rsp_data_len;
    }
    s_event_user_data.store(user_data, std::memory_order_release);
    s_event_cb.store(cb, std::memory_order_release);
    s_cfg = cfg;
    s_cfg.device_name = s_device_name;
    s_cfg.adv_data = s_adv_data_length > 0U ? s_adv_data : nullptr;
    s_cfg.adv_data_len = s_adv_data_length;
    s_cfg.scan_rsp_data = s_scan_rsp_data_length > 0U
        ? s_scan_rsp_data : nullptr;
    s_cfg.scan_rsp_data_len = s_scan_rsp_data_length;

    // GR5525 dispatches application BLE callbacks from BLE_SDK_IRQn. Keep the
    // radio IRQ timing untouched, but force the software IRQ into the
    // RTOS-callable range before any deferred callback releases an OSAL
    // semaphore. The guard prevents a pending software IRQ from running in the
    // interval where the vendor initializer may rewrite its priority.
    {
        const hal::IrqGuard guard;
        NVIC_SetPriority(BLE_SDK_IRQn, osal::kLowestIrqPriority);
        ble_stack_init(gr5525_evt_handler, &s_heaps_table);
        NVIC_SetPriority(BLE_SDK_IRQn, osal::kLowestIrqPriority);
    }

    return Status::Ok;
}

Status BleStack::set_adv_data(const uint8_t *data, size_t len) {
    if (s_stack_started.load(std::memory_order_acquire)) {
        return Status::Busy;
    }
    if ((data == nullptr && len != 0U) || len > sizeof(s_adv_data)) {
        return Status::InvalidParam;
    }
    if (len > 0U) {
        std::memcpy(s_adv_data, data, len);
    }
    s_adv_data_length = len;
    s_adv_data_configured = true;
    s_cfg.adv_data = len > 0U ? s_adv_data : nullptr;
    s_cfg.adv_data_len = len;
    return Status::Ok;
}

Status BleStack::set_scan_rsp_data(const uint8_t *data, size_t len) {
    if (s_stack_started.load(std::memory_order_acquire)) {
        return Status::Busy;
    }
    if ((data == nullptr && len != 0U)
        || len > sizeof(s_scan_rsp_data)) {
        return Status::InvalidParam;
    }
    if (len > 0U) {
        std::memcpy(s_scan_rsp_data, data, len);
    }
    s_scan_rsp_data_length = len;
    s_scan_rsp_data_configured = true;
    s_cfg.scan_rsp_data = len > 0U ? s_scan_rsp_data : nullptr;
    s_cfg.scan_rsp_data_len = len;
    return Status::Ok;
}

Status BleStack::adv_start() {
    ble_gap_adv_param_t adv_param{};
    adv_param.chnl_map = BLE_GAP_ADV_CHANNEL_37_38_39;
    adv_param.max_tx_pwr = 0;
    adv_param.disc_mode = BLE_GAP_DISC_MODE_GEN_DISCOVERABLE;
    adv_param.adv_mode = BLE_GAP_ADV_TYPE_ADV_IND;
    adv_param.filter_pol = BLE_GAP_ADV_ALLOW_SCAN_ANY_CON_ANY;
    adv_param.adv_intv_min = s_cfg.adv_interval_min;
    adv_param.adv_intv_max = s_cfg.adv_interval_max;

    if (s_cfg.adv_data && s_cfg.adv_data_len > 0) {
        const Status status = goodix::status_from_sdk(ble_gap_adv_data_set(
            0U, BLE_GAP_ADV_DATA_TYPE_DATA, s_cfg.adv_data,
            static_cast<uint16_t>(s_cfg.adv_data_len)));
        if (status != Status::Ok) {
            return status;
        }
    }
    if (s_cfg.scan_rsp_data && s_cfg.scan_rsp_data_len > 0) {
        const Status status = goodix::status_from_sdk(ble_gap_adv_data_set(
            0U, BLE_GAP_ADV_DATA_TYPE_SCAN_RSP, s_cfg.scan_rsp_data,
            static_cast<uint16_t>(s_cfg.scan_rsp_data_len)));
        if (status != Status::Ok) {
            return status;
        }
    }

    Status status = goodix::status_from_sdk(ble_gap_adv_param_set(
        0U, BLE_GAP_OWN_ADDR_STATIC, &adv_param));
    if (status != Status::Ok) {
        return status;
    }

    ble_gap_adv_time_param_t time_param{};
    time_param.duration = 0;
    time_param.max_adv_evt = 0;
    return goodix::status_from_sdk(ble_gap_adv_start(0U, &time_param));
}

Status BleStack::adv_stop() {
    return goodix::status_from_sdk(ble_gap_adv_stop(0U));
}

bool BleStack::is_connected() const {
    return s_is_connected.load(std::memory_order_acquire);
}
uint8_t BleStack::conn_index() const {
    return s_conn_idx.load(std::memory_order_acquire);
}
BdAddr BleStack::peer_addr() const {
    BdAddr snapshot {};
    uint32_t before = 0U;
    uint32_t after = 0U;
    do {
        before = s_peer_sequence.load(std::memory_order_acquire);
        if ((before & 1U) != 0U) {
            continue;
        }
        for (size_t i = 0U; i < 6U; ++i) {
            snapshot.addr[i] =
                s_peer_addr[i].load(std::memory_order_relaxed);
        }
        snapshot.addr_type =
            s_peer_addr_type.load(std::memory_order_relaxed);
        after = s_peer_sequence.load(std::memory_order_acquire);
    } while (before != after);
    return snapshot;
}

Status BleStack::conn_param_update(uint8_t conn_idx, const ConnParam &param) {
    ble_gap_conn_update_param_t p{};
    p.interval_min = param.interval_min;
    p.interval_max = param.interval_max;
    p.slave_latency = param.slave_latency;
    p.sup_timeout = param.sup_timeout;
    p.ce_len = 0x0002;
    return goodix::status_from_sdk(
        ble_gap_conn_param_update(conn_idx, &p));
}

Status BleStack::pair_accept(uint8_t conn_idx) {
    ble_sec_cfm_enc_t cfm{};
    cfm.req_type = BLE_SEC_PAIR_REQ;
    cfm.accept = true;
    return goodix::status_from_sdk(ble_sec_enc_cfm(conn_idx, &cfm));
}

Status BleStack::pair_reject(uint8_t conn_idx) {
    ble_sec_cfm_enc_t cfm{};
    cfm.req_type = BLE_SEC_PAIR_REQ;
    cfm.accept = false;
    return goodix::status_from_sdk(ble_sec_enc_cfm(conn_idx, &cfm));
}

} // namespace ble
