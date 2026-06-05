#include <drivers/can.h>
#include <soc.h>

#ifdef HAL_FDCAN_MODULE_ENABLED

namespace hal {

static constexpr uint8_t kMaxCans = 2;

struct CanInstance {
    FDCAN_HandleTypeDef handle {};
    bool started {false};
};

static CanInstance s_instances[kMaxCans];

static FDCAN_GlobalTypeDef *can_base_from_port(uint8_t port) {
    switch (port) {
    case 0: return FDCAN1;
    case 1: return FDCAN2;
    default: return nullptr;
    }
}

Can &Can::instance(uint8_t port) {
    static Can insts[kMaxCans] = {Can(0), Can(1)};
    return insts[port % kMaxCans];
}

Status Can::init(const CanConfig &config) {
    auto *base = can_base_from_port(m_port);
    if (!base) return Status::InvalidArgument;

    auto &h = s_instances[m_port % kMaxCans].handle;
    h.Instance = base;
    h.Init.FrameFormat = FDCAN_FRAME_FD_BRS;
    h.Init.Mode = FDCAN_MODE_NORMAL;
    h.Init.AutoRetransmission = ENABLE;
    h.Init.TransmitPause = DISABLE;
    h.Init.ProtocolException = DISABLE;

    h.Init.NominalPrescaler = 12;
    h.Init.NominalSyncJumpWidth = 1;
    h.Init.NominalTimeSeg1 = 15;
    h.Init.NominalTimeSeg2 = 4;

    h.Init.DataPrescaler = 3;
    h.Init.DataSyncJumpWidth = 1;
    h.Init.DataTimeSeg1 = 15;
    h.Init.DataTimeSeg2 = 4;

    h.Init.StdFiltersNbr = 1;
    h.Init.ExtFiltersNbr = 0;
    h.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;

    if (HAL_FDCAN_Init(&h) != HAL_OK) return Status::HardwareError;

    FDCAN_FilterTypeDef filter {};
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = 0;
    filter.FilterID2 = 0;
    HAL_FDCAN_ConfigFilter(&h, &filter);
    HAL_FDCAN_ConfigGlobalFilter(&h, FDCAN_ACCEPT_IN_RX_FIFO0, FDCAN_REJECT, DISABLE, DISABLE);

    m_initialized = true;
    return Status::Ok;
}

Status Can::deinit() {
    if (!m_initialized) return Status::Ok;
    auto &inst = s_instances[m_port % kMaxCans];
    if (inst.started) HAL_FDCAN_Stop(&inst.handle);
    if (HAL_FDCAN_DeInit(&inst.handle) != HAL_OK) return Status::HardwareError;
    inst.started = false;
    m_initialized = false;
    return Status::Ok;
}

Status Can::start() {
    if (!m_initialized) return Status::InvalidArgument;
    auto &h = s_instances[m_port % kMaxCans].handle;
    if (HAL_FDCAN_Start(&h) != HAL_OK) return Status::HardwareError;
    s_instances[m_port % kMaxCans].started = true;
    return Status::Ok;
}

Status Can::stop() {
    if (!m_initialized) return Status::InvalidArgument;
    auto &h = s_instances[m_port % kMaxCans].handle;
    if (HAL_FDCAN_Stop(&h) != HAL_OK) return Status::HardwareError;
    s_instances[m_port % kMaxCans].started = false;
    return Status::Ok;
}

Status Can::send(uint32_t id, const uint8_t *data, uint8_t len, uint32_t id_ext) {
    if (!m_initialized || !data || len > 64) return Status::InvalidArgument;
    auto &h = s_instances[m_port % kMaxCans].handle;

    FDCAN_TxHeaderTypeDef header {};
    header.IdType = id_ext ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    header.Identifier = id;
    header.TxFrameType = FDCAN_DATA_FRAME;
    header.DataLength = (len <= 8) ? FDCAN_DLC_BYTES_8 :
                        (len <= 16) ? FDCAN_DLC_BYTES_16 :
                        (len <= 32) ? FDCAN_DLC_BYTES_32 :
                                      FDCAN_DLC_BYTES_64;
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = FDCAN_BRS_ON;
    header.FDFormat = FDCAN_FD_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&h, &header, const_cast<uint8_t *>(data)) != HAL_OK) {
        return Status::Busy;
    }
    return Status::Ok;
}

Status Can::get_rx_message(uint32_t *id, uint8_t *data, uint8_t *len) {
    if (!m_initialized || !id || !data || !len) return Status::InvalidArgument;
    auto &h = s_instances[m_port % kMaxCans].handle;

    FDCAN_RxHeaderTypeDef header {};
    if (HAL_FDCAN_GetRxMessage(&h, FDCAN_RX_FIFO0, &header, data) != HAL_OK) {
        return Status::Timeout;
    }
    *id = header.Identifier;
    *len = static_cast<uint8_t>(header.DataLength >> 16);
    return Status::Ok;
}

} // namespace hal

#endif // HAL_FDCAN_MODULE_ENABLED
