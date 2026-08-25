#include <drivers/can.h>
#include <assert.h>
#include <soc.h>
#include <cstring>

#ifdef CONFIG_CAN_GD32

extern "C" {
#include "gd32f50x_can.h"
#include "gd32f50x_rcu.h"
}

namespace hal {

static constexpr uint8_t kMaxCans = 2;
static constexpr uint8_t kCan1FilterStartBank = 14U;

struct CanInstance {
    uint32_t base {0};
    bool started {false};
};

static CanInstance s_instances[kMaxCans];
static osal::Mutex s_hardware_mutex;
static uint8_t s_initialized_mask = 0U;

static void enable_can_clocks(uint8_t port) {
    // CAN1 uses the filter block located in the CAN0 register window.
    rcu_periph_clock_enable(RCU_CAN0);
    if (port == 1U) {
        rcu_periph_clock_enable(RCU_CAN1);
    }
}

static void release_unused_can_clocks(uint8_t port) {
    if (port == 1U) {
        rcu_periph_clock_disable(RCU_CAN1);
    }
    if (s_initialized_mask == 0U) {
        rcu_periph_clock_disable(RCU_CAN0);
    }
}

static uint32_t can_base_from_port(uint8_t port) {
    switch (port) {
    case 0: return CAN0;
    case 1: return CAN1;
    default: return 0;
    }
}

static bool timing_matches(const CanConfig &config) {
    if (config.clock_hz == 0U || config.bitrate == 0U
        || config.nominal_prescaler == 0U
        || config.nominal_time_seg1 == 0U
        || config.nominal_time_seg1 > 64U
        || config.nominal_time_seg2 == 0U
        || config.nominal_time_seg2 > 32U
        || config.nominal_sjw == 0U
        || config.nominal_sjw > 32U
        || config.nominal_sjw > config.nominal_time_seg2) {
        return false;
    }
    const uint64_t divisor = static_cast<uint64_t>(
        config.nominal_prescaler)
        * (1ULL + config.nominal_time_seg1
           + config.nominal_time_seg2);
    return static_cast<uint64_t>(config.bitrate) * divisor
        == config.clock_hz;
}

Can &Can::instance(uint8_t port) {
    static Can insts[kMaxCans] = {Can(0), Can(1)};
    static Can invalid(kMaxCans);
    return port < kMaxCans ? insts[port] : invalid;
}

Status Can::init(const CanConfig &config) {
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) return Status::Busy;
    uint32_t base = can_base_from_port(m_port);
    if (!base || m_initialized.load(std::memory_order_acquire)
        || !timing_matches(config)) {
        return Status::InvalidArgument;
    }
    osal::LockGuard hardware_lock(s_hardware_mutex);
    if (!hardware_lock.owns_lock()) return Status::Busy;
    enable_can_clocks(m_port);

    s_instances[m_port].base = base;

    // Enter init mode
    if (can_working_mode_set(base, CAN_MODE_INITIALIZE) != SUCCESS) {
        s_instances[m_port].base = 0U;
        release_unused_can_clocks(m_port);
        return Status::HardwareError;
    }

    // Configure bit timing
    can_parameter_struct params;
    can_struct_para_init(CAN_INIT_STRUCT, &params);

    params.working_mode = CAN_NORMAL_MODE;
    params.prescaler = config.nominal_prescaler;
    params.time_segment_1 = static_cast<uint8_t>(
        config.nominal_time_seg1 - 1U);
    params.time_segment_2 = static_cast<uint8_t>(
        config.nominal_time_seg2 - 1U);
    params.resync_jump_width = static_cast<uint8_t>(
        config.nominal_sjw - 1U);

    params.time_triggered = DISABLE;
    params.auto_bus_off_recovery = ENABLE;
    params.auto_wake_up = DISABLE;
    params.auto_retrans = ENABLE;
    params.rec_fifo_overwrite = DISABLE;
    params.trans_fifo_order = DISABLE;

    if (can_init(base, &params) != SUCCESS) {
        can_deinit(base);
        s_instances[m_port].base = 0U;
        release_unused_can_clocks(m_port);
        return Status::HardwareError;
    }

    // Split the shared filter bank deterministically between controllers.
    can1_filter_start_bank(kCan1FilterStartBank);
    const uint16_t filter = m_port == 0U ? 0U : kCan1FilterStartBank;
    can_filter_mask_mode_init(0U, 0U, CAN_STANDARD_FIFO0, filter);

    s_initialized_mask |= static_cast<uint8_t>(1U << m_port);
    m_initialized.store(true, std::memory_order_release);
    return Status::Ok;
}

Status Can::deinit() {
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) return Status::Busy;
    if (!m_initialized.load(std::memory_order_acquire)) return Status::Ok;
    osal::LockGuard hardware_lock(s_hardware_mutex);
    if (!hardware_lock.owns_lock()) return Status::Busy;
    uint32_t base = s_instances[m_port].base;
    if (s_instances[m_port].started) {
        if (can_working_mode_set(base, CAN_MODE_SLEEP) != SUCCESS) {
            return Status::HardwareError;
        }
    }
    can_deinit(base);
    s_instances[m_port].started = false;
    s_instances[m_port].base = 0U;
    s_initialized_mask &= static_cast<uint8_t>(~(1U << m_port));
    release_unused_can_clocks(m_port);
    m_initialized.store(false, std::memory_order_release);
    return Status::Ok;
}

bool Can::is_started() const {
    osal::LockGuard lock(m_operation_mutex, 0U);
    return lock.owns_lock()
        && m_initialized.load(std::memory_order_acquire) && m_port < kMaxCans
        && s_instances[m_port].started;
}

Status Can::start() {
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) return Status::Busy;
    if (!m_initialized.load(std::memory_order_acquire)) {
        return Status::InvalidArgument;
    }
    if (s_instances[m_port].started) return Status::Ok;
    uint32_t base = s_instances[m_port].base;
    if (can_working_mode_set(base, CAN_MODE_NORMAL) != SUCCESS) {
        return Status::HardwareError;
    }
    s_instances[m_port].started = true;
    return Status::Ok;
}

Status Can::stop() {
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) return Status::Busy;
    if (!m_initialized.load(std::memory_order_acquire)) {
        return Status::InvalidArgument;
    }
    if (!s_instances[m_port].started) return Status::Ok;
    uint32_t base = s_instances[m_port].base;
    if (can_working_mode_set(base, CAN_MODE_SLEEP) != SUCCESS) {
        return Status::HardwareError;
    }
    s_instances[m_port].started = false;
    return Status::Ok;
}

Status Can::send(uint32_t id, const uint8_t *data, uint8_t len, uint32_t id_ext) {
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) return Status::Busy;
    HAL_ASSERT_MSG(m_initialized.load(std::memory_order_acquire),
                   "CAN not initialized");
    if (!m_initialized.load(std::memory_order_acquire)
        || (len > 0U && data == nullptr) || len > 8U
        || !s_instances[m_port].started
        || id_ext > 1U
        || (id_ext == 0U && id > CAN_SFID_MASK)
        || (id_ext != 0U && id > CAN_EFID_MASK)) {
        return Status::InvalidArgument;
    }

    uint32_t base = s_instances[m_port].base;

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
    if (len > 0U) {
        memcpy(tx_msg.tx_data, data, len);
    }

    tx_msg.fd_flag = CAN_FDF_CLASSIC;
    tx_msg.fd_brs = CAN_BRS_DISABLE;
    tx_msg.fd_esi = CAN_ESI_DOMINANT;

    uint8_t mailbox = can_message_transmit(base, &tx_msg);
    if (mailbox == CAN_NOMAILBOX) {
        return Status::Busy;
    }
    return Status::Ok;
}

Status Can::get_rx_message(uint32_t *id, uint8_t *data,
                           uint8_t capacity, uint8_t *len,
                           bool *is_extended) {
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) return Status::Busy;
    if (!m_initialized.load(std::memory_order_acquire)
        || !id || !data || !len
        || !s_instances[m_port].started) {
        return Status::InvalidArgument;
    }
    *len = 0U;

    uint32_t base = s_instances[m_port].base;

    // Check if FIFO0 has messages
    if (can_receive_message_length_get(base, CAN_FIFO0) == 0) {
        return Status::Timeout;
    }

    can_receive_message_struct rx_msg;
    memset(&rx_msg, 0, sizeof(rx_msg));

    can_message_receive(base, CAN_FIFO0, &rx_msg);

    if (rx_msg.rx_ft == CAN_FT_REMOTE
        || (rx_msg.rx_ff != CAN_FF_STANDARD
            && rx_msg.rx_ff != CAN_FF_EXTENDED)) {
        return Status::HardwareError;
    }
    const bool extended = rx_msg.rx_ff == CAN_FF_EXTENDED;
    if (extended) {
        *id = rx_msg.rx_efid;
    } else {
        *id = rx_msg.rx_sfid;
    }
    if (is_extended != nullptr) {
        *is_extended = extended;
    }

    if (rx_msg.rx_dlen > 8U) {
        return Status::HardwareError;
    }
    *len = rx_msg.rx_dlen;
    if (rx_msg.rx_dlen > capacity) {
        return Status::NoMemory;
    }
    memcpy(data, rx_msg.rx_data, rx_msg.rx_dlen);

    return Status::Ok;
}

} // namespace hal

#endif // CONFIG_CAN_GD32
