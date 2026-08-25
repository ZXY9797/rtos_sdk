#include <drivers/can.h>
#include <drivers/can_dlc.h>
#include <soc.h>

#include <cstring>

#if defined(CONFIG_CAN_STM32) && !defined(HAL_FDCAN_MODULE_ENABLED)
#error "CONFIG_CAN_STM32 requires HAL_FDCAN_MODULE_ENABLED"
#endif

#if defined(CONFIG_CAN_STM32)

namespace hal {
namespace {

constexpr uint8_t kMaxCans = 2U;
constexpr uint32_t kStandardIdMax = 0x7FFU;
constexpr uint32_t kExtendedIdMax = 0x1FFFFFFFU;
constexpr uint32_t kMessageRamStrideWords = 512U;
constexpr uint32_t kRxFifoDepth = 8U;
constexpr uint32_t kTxFifoDepth = 8U;

struct CanInstance {
    FDCAN_HandleTypeDef handle {};
    bool started {false};
};

CanInstance instances[kMaxCans] {};
osal::Mutex hardware_mutex;
uint8_t initialized_mask = 0U;

void enable_can_clock()
{
    __HAL_RCC_FDCAN_CLK_ENABLE();
}

void release_unused_can_clock()
{
    if (initialized_mask == 0U) {
        __HAL_RCC_FDCAN_CLK_DISABLE();
    }
}

FDCAN_GlobalTypeDef* can_base_from_port(uint8_t port)
{
    switch (port) {
    case 0U: return FDCAN1;
    case 1U: return FDCAN2;
    default: return nullptr;
    }
}

bool timing_matches(uint32_t clock_hz, uint32_t bitrate,
                    uint16_t prescaler, uint8_t seg1, uint8_t seg2)
{
    if (clock_hz == 0U || bitrate == 0U || prescaler == 0U
        || seg1 == 0U || seg2 == 0U) {
        return false;
    }
    const uint64_t divisor = static_cast<uint64_t>(prescaler)
        * (1ULL + seg1 + seg2);
    return divisor != 0U
        && static_cast<uint64_t>(bitrate) * divisor == clock_hz;
}

uint32_t hal_dlc(uint8_t dlc)
{
    constexpr uint32_t values[] = {
        FDCAN_DLC_BYTES_0, FDCAN_DLC_BYTES_1, FDCAN_DLC_BYTES_2,
        FDCAN_DLC_BYTES_3, FDCAN_DLC_BYTES_4, FDCAN_DLC_BYTES_5,
        FDCAN_DLC_BYTES_6, FDCAN_DLC_BYTES_7, FDCAN_DLC_BYTES_8,
        FDCAN_DLC_BYTES_12, FDCAN_DLC_BYTES_16, FDCAN_DLC_BYTES_20,
        FDCAN_DLC_BYTES_24, FDCAN_DLC_BYTES_32, FDCAN_DLC_BYTES_48,
        FDCAN_DLC_BYTES_64,
    };
    return dlc < (sizeof(values) / sizeof(values[0])) ? values[dlc] : 0U;
}

} // namespace

Can& Can::instance(uint8_t port)
{
    static Can controllers[kMaxCans] = {Can(0U), Can(1U)};
    static Can invalid(kMaxCans);
    return port < kMaxCans ? controllers[port] : invalid;
}

Status Can::init(const CanConfig& config)
{
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) {
        return Status::Busy;
    }
    FDCAN_GlobalTypeDef* const base = can_base_from_port(m_port);
    if (base == nullptr || m_initialized.load(std::memory_order_acquire)
        || !timing_matches(config.clock_hz, config.bitrate,
                           config.nominal_prescaler,
                           config.nominal_time_seg1,
                           config.nominal_time_seg2)
        || !timing_matches(config.clock_hz, config.data_bitrate,
                           config.data_prescaler,
                           config.data_time_seg1,
                           config.data_time_seg2)
        || config.nominal_sjw == 0U
        || config.nominal_sjw > config.nominal_time_seg2
        || config.data_sjw == 0U
        || config.data_sjw > config.data_time_seg2
        || config.nominal_prescaler > 512U
        || config.nominal_time_seg2 > 128U
        || config.nominal_sjw > 128U
        || config.data_prescaler > 32U
        || config.data_time_seg1 > 32U
        || config.data_time_seg2 > 16U
        || config.data_sjw > 16U) {
        return Status::InvalidArgument;
    }
    osal::LockGuard hardware_lock(hardware_mutex);
    if (!hardware_lock.owns_lock()) {
        return Status::Busy;
    }
    enable_can_clock();

    CanInstance& instance = instances[m_port];
    FDCAN_HandleTypeDef& handle = instance.handle;
    handle = {};
    handle.Instance = base;
    handle.Init.FrameFormat = FDCAN_FRAME_FD_BRS;
    handle.Init.Mode = FDCAN_MODE_NORMAL;
    handle.Init.AutoRetransmission = ENABLE;
    handle.Init.TransmitPause = DISABLE;
    handle.Init.ProtocolException = DISABLE;
    handle.Init.NominalPrescaler = config.nominal_prescaler;
    handle.Init.NominalSyncJumpWidth = config.nominal_sjw;
    handle.Init.NominalTimeSeg1 = config.nominal_time_seg1;
    handle.Init.NominalTimeSeg2 = config.nominal_time_seg2;
    handle.Init.DataPrescaler = config.data_prescaler;
    handle.Init.DataSyncJumpWidth = config.data_sjw;
    handle.Init.DataTimeSeg1 = config.data_time_seg1;
    handle.Init.DataTimeSeg2 = config.data_time_seg2;
    // Each controller owns a disjoint 512-word window in the 2560-word
    // shared FDCAN message RAM. One window uses 289 words with this layout.
    handle.Init.MessageRAMOffset = static_cast<uint32_t>(m_port)
        * kMessageRamStrideWords;
    handle.Init.StdFiltersNbr = 1U;
    handle.Init.ExtFiltersNbr = 0U;
    handle.Init.RxFifo0ElmtsNbr = kRxFifoDepth;
    handle.Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_64;
    handle.Init.RxFifo1ElmtsNbr = 0U;
    handle.Init.RxFifo1ElmtSize = FDCAN_DATA_BYTES_64;
    handle.Init.RxBuffersNbr = 0U;
    handle.Init.RxBufferSize = FDCAN_DATA_BYTES_64;
    handle.Init.TxEventsNbr = 0U;
    handle.Init.TxBuffersNbr = 0U;
    handle.Init.TxFifoQueueElmtsNbr = kTxFifoDepth;
    handle.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    handle.Init.TxElmtSize = FDCAN_DATA_BYTES_64;

    if (HAL_FDCAN_Init(&handle) != HAL_OK) {
        (void)HAL_FDCAN_DeInit(&handle);
        release_unused_can_clock();
        return Status::HardwareError;
    }

    FDCAN_FilterTypeDef filter {};
    filter.IdType = FDCAN_STANDARD_ID;
    filter.FilterIndex = 0U;
    filter.FilterType = FDCAN_FILTER_MASK;
    filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    filter.FilterID1 = 0U;
    filter.FilterID2 = 0U;
    if (HAL_FDCAN_ConfigFilter(&handle, &filter) != HAL_OK
        || HAL_FDCAN_ConfigGlobalFilter(
               &handle, FDCAN_ACCEPT_IN_RX_FIFO0,
               FDCAN_ACCEPT_IN_RX_FIFO0,
               FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK) {
        (void)HAL_FDCAN_DeInit(&handle);
        release_unused_can_clock();
        return Status::HardwareError;
    }

    instance.started = false;
    initialized_mask |= static_cast<uint8_t>(1U << m_port);
    m_initialized.store(true, std::memory_order_release);
    return Status::Ok;
}

Status Can::deinit()
{
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) {
        return Status::Busy;
    }
    if (!m_initialized.load(std::memory_order_acquire)) {
        return Status::Ok;
    }
    osal::LockGuard hardware_lock(hardware_mutex);
    if (!hardware_lock.owns_lock()) {
        return Status::Busy;
    }
    CanInstance& instance = instances[m_port];
    if (instance.started && HAL_FDCAN_Stop(&instance.handle) != HAL_OK) {
        return Status::HardwareError;
    }
    instance.started = false;
    if (HAL_FDCAN_DeInit(&instance.handle) != HAL_OK) {
        return Status::HardwareError;
    }
    initialized_mask &= static_cast<uint8_t>(~(1U << m_port));
    release_unused_can_clock();
    m_initialized.store(false, std::memory_order_release);
    return Status::Ok;
}

bool Can::is_started() const
{
    osal::LockGuard lock(m_operation_mutex, 0U);
    return lock.owns_lock()
        && m_initialized.load(std::memory_order_acquire) && m_port < kMaxCans
        && instances[m_port].started;
}

Status Can::start()
{
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) {
        return Status::Busy;
    }
    if (!m_initialized.load(std::memory_order_acquire)) {
        return Status::InvalidArgument;
    }
    CanInstance& instance = instances[m_port];
    if (instance.started) {
        return Status::Ok;
    }
    if (HAL_FDCAN_Start(&instance.handle) != HAL_OK) {
        return Status::HardwareError;
    }
    instance.started = true;
    return Status::Ok;
}

Status Can::stop()
{
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) {
        return Status::Busy;
    }
    if (!m_initialized.load(std::memory_order_acquire)) {
        return Status::InvalidArgument;
    }
    CanInstance& instance = instances[m_port];
    if (!instance.started) {
        return Status::Ok;
    }
    if (HAL_FDCAN_Stop(&instance.handle) != HAL_OK) {
        return Status::HardwareError;
    }
    instance.started = false;
    return Status::Ok;
}

Status Can::send(uint32_t id, const uint8_t* data, uint8_t len,
                 uint32_t id_ext)
{
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) {
        return Status::Busy;
    }
    if (!m_initialized.load(std::memory_order_acquire)
        || !instances[m_port].started
        || (len > 0U && data == nullptr)
        || id_ext > 1U
        || (id_ext == 0U && id > kStandardIdMax)
        || (id_ext != 0U && id > kExtendedIdMax)) {
        return Status::InvalidArgument;
    }
    uint8_t dlc = 0U;
    uint8_t padded_length = 0U;
    if (!can_fd_length_to_dlc(len, dlc, padded_length)) {
        return Status::InvalidArgument;
    }

    uint8_t payload[64] {};
    if (len > 0U) {
        std::memcpy(payload, data, len);
    }
    FDCAN_TxHeaderTypeDef header {};
    header.IdType = id_ext != 0U
        ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
    header.Identifier = id;
    header.TxFrameType = FDCAN_DATA_FRAME;
    header.DataLength = hal_dlc(dlc);
    header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    header.BitRateSwitch = FDCAN_BRS_ON;
    header.FDFormat = FDCAN_FD_CAN;
    header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    header.MessageMarker = 0U;
    (void)padded_length;

    return HAL_FDCAN_AddMessageToTxFifoQ(
        &instances[m_port].handle, &header, payload) == HAL_OK
        ? Status::Ok : Status::Busy;
}

Status Can::get_rx_message(uint32_t* id, uint8_t* data,
                           uint8_t capacity, uint8_t* len,
                           bool* is_extended)
{
    osal::LockGuard lock(m_operation_mutex);
    if (!lock.owns_lock()) {
        return Status::Busy;
    }
    if (!m_initialized.load(std::memory_order_acquire)
        || !instances[m_port].started
        || id == nullptr || data == nullptr || len == nullptr) {
        return Status::InvalidArgument;
    }
    *len = 0U;

    uint8_t payload[64] {};
    FDCAN_RxHeaderTypeDef header {};
    if (HAL_FDCAN_GetRxMessage(&instances[m_port].handle, FDCAN_RX_FIFO0,
                               &header, payload) != HAL_OK) {
        return Status::Timeout;
    }
    if (header.RxFrameType != FDCAN_DATA_FRAME
        || (header.IdType != FDCAN_STANDARD_ID
            && header.IdType != FDCAN_EXTENDED_ID)
        || (header.IdType == FDCAN_STANDARD_ID
            && header.Identifier > kStandardIdMax)
        || (header.IdType == FDCAN_EXTENDED_ID
            && header.Identifier > kExtendedIdMax)) {
        return Status::HardwareError;
    }
    uint8_t received_length = 0U;
    const uint8_t dlc = static_cast<uint8_t>(header.DataLength >> 16U);
    if (!can_fd_dlc_to_length(dlc, received_length)) {
        *len = 0U;
        return Status::HardwareError;
    }
    *len = received_length;
    if (received_length > capacity) {
        return Status::NoMemory;
    }
    *id = header.Identifier;
    if (is_extended != nullptr) {
        *is_extended = header.IdType == FDCAN_EXTENDED_ID;
    }
    if (received_length > 0U) {
        std::memcpy(data, payload, received_length);
    }
    return Status::Ok;
}

} // namespace hal

#endif // CONFIG_CAN_STM32
