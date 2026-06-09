/**
 * BleStack implementation for Goodix GR5525.
 *
 * Wraps ble_stack_init(), GAP parameters, advertising, and connection management.
 * Translates GR5525 BLE events into vendor-neutral ble::Event.
 */

#include "ble/ble_stack.h"

extern "C" {
#include "ble.h"
#include "platform_sdk.h"
#include "gr55xx_pwr.h"
#include "app_log.h"
}

#include "ble_cfg.h"

/* Stub for BLE SDK internal logging — not provided by the SDK library */
extern "C" void stack_raw_log_output(const char *fmt, ...) { (void)fmt; }

namespace ble {

/* ---- GR5525 BLE heap (must be global, allocated before ble_stack_init) ---- */
static STACK_HEAP_INIT(s_heaps_table);

/* ---- Static state (ISR/callback-safe: volatile for cross-context access) ---- */
static volatile EventCallback s_event_cb = nullptr;
static volatile void *s_event_user_data = nullptr;
static volatile bool s_is_connected = false;
static volatile uint8_t s_conn_idx = 0;
static BdAddr s_peer_addr{}; // 仅在 BLE 回调中写入，读取时已稳定
static StackConfig s_cfg{};

/* ---- GR5525 event → ble::Event translation ---- */
static void gr5525_evt_handler(const ble_evt_t *p_evt) {
    Event evt{};
    evt.status = static_cast<uint8_t>(p_evt->evt_status);

    switch (p_evt->evt_id) {
    case BLE_COMMON_EVT_STACK_INIT:
        evt.id = EventId::StackInit;
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
        s_is_connected = true;
        s_conn_idx = evt.conn_idx;
        memcpy(s_peer_addr.addr,
               p_evt->evt.gapc_evt.params.connected.peer_addr.addr, 6);
        s_peer_addr.addr_type = p_evt->evt.gapc_evt.params.connected.peer_addr_type;
        evt.peer_addr = s_peer_addr;
        break;

    case BLE_GAPC_EVT_DISCONNECTED:
        evt.id = EventId::Disconnected;
        evt.conn_idx = p_evt->evt.gapc_evt.index;
        evt.disconnect_reason = p_evt->evt.gapc_evt.params.disconnected.reason;
        s_is_connected = false;
        break;

    case BLE_GAPC_EVT_CONN_PARAM_UPDATE_REQ:
        ble_gap_conn_param_update_reply(p_evt->evt.gapc_evt.index, true);
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
        ble_sec_enc_cfm(p_evt->evt.sec_evt.index, &cfm);
        break;
    }

    case BLE_SEC_EVT_LINK_ENCRYPTED:
        evt.id = (p_evt->evt_status == BLE_SUCCESS) ? EventId::PairSuccess : EventId::PairFailed;
        evt.conn_idx = p_evt->evt.sec_evt.index;
        break;

    default:
        return; // Don't dispatch unhandled events
    }

    if (s_event_cb) {
        s_event_cb(evt, const_cast<void *>(static_cast<const volatile void *>(s_event_user_data)));
    }
}

/* ---- BleStack implementation ---- */

Status BleStack::init(const StackConfig &cfg, EventCallback cb, void *user_data) {
    s_event_cb = cb;
    s_event_user_data = user_data;
    s_cfg = cfg;

    // Initialize BLE stack (creates internal BLE task)
    ble_stack_init(gr5525_evt_handler, &s_heaps_table);

    // Configure GAP device name
    // SDK API accepts non-const uint8_t* but does not modify the buffer
    ble_gap_device_name_set(BLE_GAP_WRITE_PERM_DISABLE,
                            reinterpret_cast<uint8_t *>(
                                const_cast<char *>(cfg.device_name)),
                            strlen(cfg.device_name));

    // Configure connection parameters
    ble_gap_conn_param_t conn_param{};
    conn_param.interval_min = cfg.conn_param.interval_min;
    conn_param.interval_max = cfg.conn_param.interval_max;
    conn_param.slave_latency = cfg.conn_param.slave_latency;
    conn_param.sup_timeout = cfg.conn_param.sup_timeout;
    ble_gap_ppcp_set(&conn_param);

    // Configure security parameters
    ble_sec_param_t sec_param{};
    switch (cfg.sec_param.level) {
    case SecParam::Level::None:
        sec_param.level = BLE_SEC_MODE1_LEVEL1;
        break;
    case SecParam::Level::Open:
        sec_param.level = BLE_SEC_MODE1_LEVEL1;
        break;
    case SecParam::Level::Mitm:
        sec_param.level = BLE_SEC_MODE1_LEVEL2;
        break;
    case SecParam::Level::SecureConn:
        sec_param.level = BLE_SEC_MODE1_LEVEL3;
        break;
    }
    sec_param.auth = cfg.sec_param.bonding ? BLE_SEC_AUTH_BOND : BLE_SEC_AUTH_NONE;
    sec_param.io_cap = BLE_SEC_IO_NO_INPUT_NO_OUTPUT;
    sec_param.key_size = 16;
    sec_param.ikey_dist = BLE_SEC_KDIST_ENCKEY | BLE_SEC_KDIST_IDKEY;
    sec_param.rkey_dist = BLE_SEC_KDIST_ENCKEY | BLE_SEC_KDIST_IDKEY;
    ble_sec_params_set(&sec_param);

    ble_gap_privacy_params_set(150, true);

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
        ble_gap_adv_data_set(0, BLE_GAP_ADV_DATA_TYPE_DATA,
                             s_cfg.adv_data, s_cfg.adv_data_len);
    }
    if (s_cfg.scan_rsp_data && s_cfg.scan_rsp_data_len > 0) {
        ble_gap_adv_data_set(0, BLE_GAP_ADV_DATA_TYPE_SCAN_RSP,
                             s_cfg.scan_rsp_data, s_cfg.scan_rsp_data_len);
    }

    ble_gap_adv_param_set(0, BLE_GAP_OWN_ADDR_STATIC, &adv_param);

    ble_gap_adv_time_param_t time_param{};
    time_param.duration = 0;
    time_param.max_adv_evt = 0;
    ble_gap_adv_start(0, &time_param);

    return Status::Ok;
}

Status BleStack::adv_stop() {
    ble_gap_adv_stop(0);
    return Status::Ok;
}

bool BleStack::is_connected() const { return s_is_connected; }
uint8_t BleStack::conn_index() const { return s_conn_idx; }
BdAddr BleStack::peer_addr() const { return s_peer_addr; }

Status BleStack::conn_param_update(uint8_t conn_idx, const ConnParam &param) {
    ble_gap_conn_update_param_t p{};
    p.interval_min = param.interval_min;
    p.interval_max = param.interval_max;
    p.slave_latency = param.slave_latency;
    p.sup_timeout = param.sup_timeout;
    p.ce_len = 0x0002;
    ble_gap_conn_param_update(conn_idx, &p);
    return Status::Ok;
}

Status BleStack::pair_accept(uint8_t conn_idx) {
    ble_sec_cfm_enc_t cfm{};
    cfm.req_type = BLE_SEC_PAIR_REQ;
    cfm.accept = true;
    ble_sec_enc_cfm(conn_idx, &cfm);
    return Status::Ok;
}

Status BleStack::pair_reject(uint8_t conn_idx) {
    ble_sec_cfm_enc_t cfm{};
    cfm.req_type = BLE_SEC_PAIR_REQ;
    cfm.accept = false;
    ble_sec_enc_cfm(conn_idx, &cfm);
    return Status::Ok;
}

} // namespace ble
