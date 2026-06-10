#include <drivers/can.h>
#include <assert.h>
#include <soc.h>
#include <cstring>

#ifdef CONFIG_CAN_GD32

#include "gd32f50x_can.h"

namespace hal {

static constexpr uint8_t kMaxCans = 2;

struct CanInstance {
    uint32_t base {0};
    bool started {false};
};

static CanInstance s_instances[kMaxCans];

static uint32_t can_base_from_port(uint8_t port) {
    switch (port) {
    case 0: return CAN0;
    case 1: return CAN1;
    default: return 0;
    }
}

Can &Can::instance(uint8_t port) {
    static Can insts[kMaxCans] = {Can(0), Can(1)};
    return insts[port % kMaxCans];
}

Status Can::init(const CanConfig &config) {
    uint32_t base = can_base_from_port(m_port);
    if (!base) return Status::InvalidArgument;

    s_instances[m_port % kMaxCans].base = base;

    // Enter init mode
    can_working_mode_set(base, CAN_MODE_INITIALIZE);

    // Configure bit timing
    // GD32 CAN clock = APB1 = 100MHz (200MHz / 2)
    // 500kbps: prescaler=10, BS1=15, BS2=4 -> 100MHz/(10*(1+15+4)) = 500kbps
    can_parameter_struct params;
    can_struct_para_init(CAN_INIT_STRUCT, &params);

    params.working_mode = CAN_NORMAL_MODE;
    params.prescaler = 10;
    params.time_segment_1 = CAN_BT_BS1_15TQ;
    params.time_segment_2 = CAN_BT_BS2_4TQ;
    params.resync_jump_width = CAN_BT_SJW_1TQ;

    params.time_triggered = DISABLE;
    params.auto_bus_off_recovery = ENABLE;
    params.auto_wake_up = DISABLE;
    params.auto_retrans = ENABLE;
    params.rec_fifo_overwrite = DISABLE;
    params.trans_fifo_order = DISABLE;

    can_init(base, &params);

    // Configure filter 0: accept all standard IDs into FIFO0
    can_filter_mask_mode_init(0, 0, CAN_STANDARD_FIFO0, 0);

    m_initialized = true;
    return Status::Ok;
}

Status Can::deinit() {
    if (!m_initialized) return Status::Ok;
    uint32_t base = s_instances[m_port % kMaxCans].base;
    if (s_instances[m_port % kMaxCans].started) {
        can_working_mode_set(base, CAN_MODE_SLEEP);
    }
    can_deinit(base);
    s_instances[m_port % kMaxCans].started = false;
    m_initialized = false;
    return Status::Ok;
}

Status Can::start() {
    if (!m_initialized) return Status::InvalidArgument;
    uint32_t base = s_instances[m_port % kMaxCans].base;
    can_working_mode_set(base, CAN_MODE_NORMAL);
    s_instances[m_port % kMaxCans].started = true;
    return Status::Ok;
}

Status Can::stop() {
    if (!m_initialized) return Status::InvalidArgument;
    uint32_t base = s_instances[m_port % kMaxCans].base;
    can_working_mode_set(base, CAN_MODE_SLEEP);
    s_instances[m_port % kMaxCans].started = false;
    return Status::Ok;
}

Status Can::send(uint32_t id, const uint8_t *data, uint8_t len, uint32_t id_ext) {
    HAL_ASSERT_MSG(m_initialized, "CAN not initialized");
    if (!m_initialized || !data || len > 8) return Status::InvalidArgument;

    uint32_t base = s_instances[m_port % kMaxCans].base;

    can_transmit_message_struct tx_msg;
    memset(&tx_msg, 0, sizeof(tx_msg));

    if (id_ext) {
        tx_msg.tx_ff = CAN_FF_EXTENDED;
        tx_msg.tx_efid = id & CAN_EFID_MASK;
    } else {
        tx_msg.tx_ff = CAN_FF_STANDARD;
        tx_msg.tx_sfid = id & CAN_SFID_MASK;
    }

    tx_msg.tx_ft = CAN_FT_DATA;
    tx_msg.tx_dlen = len;
    memcpy(tx_msg.tx_data, data, len);

    tx_msg.fd_flag = CAN_FDF_CLASSIC;
    tx_msg.fd_brs = CAN_BRS_DISABLE;
    tx_msg.fd_esi = CAN_ESI_DOMINANT;

    uint8_t mailbox = can_message_transmit(base, &tx_msg);
    if (mailbox == CAN_NOMAILBOX) {
        return Status::Busy;
    }
    return Status::Ok;
}

Status Can::get_rx_message(uint32_t *id, uint8_t *data, uint8_t *len) {
    if (!m_initialized || !id || !data || !len) return Status::InvalidArgument;

    uint32_t base = s_instances[m_port % kMaxCans].base;

    // Check if FIFO0 has messages
    if (can_receive_message_length_get(base, CAN_FIFO0) == 0) {
        return Status::Timeout;
    }

    can_receive_message_struct rx_msg;
    memset(&rx_msg, 0, sizeof(rx_msg));

    can_message_receive(base, CAN_FIFO0, &rx_msg);

    if (rx_msg.rx_ff == CAN_FF_EXTENDED) {
        *id = rx_msg.rx_efid;
    } else {
        *id = rx_msg.rx_sfid;
    }

    *len = rx_msg.rx_dlen;
    memcpy(data, rx_msg.rx_data, rx_msg.rx_dlen);

    return Status::Ok;
}

} // namespace hal

#endif // CONFIG_CAN_GD32
