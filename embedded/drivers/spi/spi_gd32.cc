#include <drivers/spi.h>
#include <drivers/dma.h>
#include <assert.h>
#include <irq.h>
#include <osal.h>

#include "gd32_regs.h"
#include "gd32f50x_dma.h"

namespace hal {
namespace {

/* CTL0 bits */
constexpr uint32_t CTL0_CKPH    = (1U << 0);
constexpr uint32_t CTL0_CKPL    = (1U << 1);
constexpr uint32_t CTL0_MSTMOD  = (1U << 2);
constexpr uint32_t CTL0_PSC_Msk = (7U << 3);
constexpr uint32_t CTL0_SPIEN   = (1U << 6);
constexpr uint32_t CTL0_SWNSS   = (1U << 8);
constexpr uint32_t CTL0_SWNSSEN = (1U << 9);
constexpr uint32_t CTL0_FF16    = (1U << 11);

/* CTL1 bits */
constexpr uint32_t CTL1_DMAREN = (1U << 0);
constexpr uint32_t CTL1_DMATEN = (1U << 1);

/* STAT bits */
constexpr uint32_t STAT_TRANS = (1U << 7);

void spi_clock_enable(uintptr_t base) {
    auto &apb1 = gd32::rcu_apb1en();
    auto &apb2 = gd32::rcu_apb2en();
    switch (base) {
        case SPI0_BASE: apb2 |= gd32::clk::SPI0EN; break;
        case SPI1_BASE: apb1 |= gd32::clk::SPI1EN; break;
        case SPI2_BASE: apb1 |= gd32::clk::SPI2EN; break;
        default: break;
    }
}

[[nodiscard]] bool dma_channel_isr(
    const DmaChannelConfig &config,
    std::atomic<uint8_t> &done_mask,
    std::atomic<uint8_t> &error_mask,
    uint8_t channel_bit)
{
    if (!config.is_valid() || config.channel > 6U) {
        return false;
    }

    auto *dma = reinterpret_cast<gd32::DmaRegs *>(config.controller);
    const uint32_t flags = dma->INTF >> (config.channel * 4U);
    if ((flags & DMA_INTF_ERRIF) != 0U) {
        error_mask.fetch_or(channel_bit, std::memory_order_relaxed);
    }
    if ((flags & (DMA_INTF_FTFIF | DMA_INTF_ERRIF)) == 0U) {
        return false;
    }
    done_mask.fetch_or(channel_bit, std::memory_order_relaxed);
    dma->INTC = (DMA_INTC_GIFC | DMA_INTC_FTFIFC
                 | DMA_INTC_HTFIFC | DMA_INTC_ERRIFC)
                << (config.channel * 4U);
    return true;
}

} // anonymous namespace

Status SpiBase::init(const SpiConfig &config) {
    HAL_ASSERT(m_base != 0);
    if (config.clock_hz == 0U
        || config.data_bits != 8U
        || !config.dma_tx.is_valid() || !config.dma_rx.is_valid() ||
        config.dma_tx.channel > 6U || config.dma_rx.channel > 6U ||
        (config.dma_tx.controller == config.dma_rx.controller &&
         config.dma_tx.channel == config.dma_rx.channel)) {
        return Status::InvalidArgument;
    }
    if (!m_bus_mutex.is_valid() || !m_xfer_sem.is_valid()) {
        return Status::NoMemory;
    }

    m_dma_tx = config.dma_tx;
    m_dma_rx = config.dma_rx;
    m_async_tx_channel = DmaChannel(
        static_cast<uint32_t>(m_dma_tx.controller),
        m_dma_tx.channel, m_dma_tx.mux_channel);
    m_async_rx_channel = DmaChannel(
        static_cast<uint32_t>(m_dma_rx.controller),
        m_dma_rx.channel, m_dma_rx.mux_channel);
    m_config = config;

    auto *regs = reinterpret_cast<gd32::SpiRegs *>(m_base);
    spi_clock_enable(m_base);

    regs->CTL0 &= ~CTL0_SPIEN;

    uint32_t ctl0 = CTL0_MSTMOD | CTL0_SWNSS | CTL0_SWNSSEN;

    if (config.mode == SpiMode::Mode1 || config.mode == SpiMode::Mode3) {
        ctl0 |= CTL0_CKPH;
    }
    if (config.mode == SpiMode::Mode2 || config.mode == SpiMode::Mode3) {
        ctl0 |= CTL0_CKPL;
    }
    if (config.data_bits == 16) ctl0 |= CTL0_FF16;

    uint32_t pclk = m_base == SPI0_BASE
        ? SystemCoreClock : (SystemCoreClock / 2U);
    uint32_t psc = 0;
    uint32_t div = 2;
    while (div < 256 && (pclk / div) > config.clock_hz) { psc++; div <<= 1; }
    if (psc > 7) psc = 7;
    ctl0 |= (psc << 3U) & CTL0_PSC_Msk;

    regs->CTL0 = ctl0;
    regs->CTL1 |= CTL1_DMATEN | CTL1_DMAREN;
    regs->CTL0 |= CTL0_SPIEN;

    set_state(DeviceState::Initialized);
    return Status::Ok;
}

Status SpiBase::deinit() {
    if (!is_initialized()) return Status::Ok;
    (void)end_async();
    auto *regs = reinterpret_cast<gd32::SpiRegs *>(m_base);
    regs->CTL0 &= ~CTL0_SPIEN;
    set_state(DeviceState::Created);
    return Status::Ok;
}

Status SpiBase::configure_sync_dma(
    const uint8_t *tx, uint8_t *rx, size_t len,
    DmaChannel& tx_channel, DmaChannel& rx_channel)
{
    auto* regs = reinterpret_cast<gd32::SpiRegs*>(m_base);
    const uint8_t* tx_buffer = tx != nullptr ? tx : &m_sync_dummy_tx;
    uint8_t* rx_buffer = rx != nullptr ? rx : &m_sync_dummy_rx;
    const DmaConfig rx_config = {
        .request_id = m_dma_rx.request_id,
        .periph_addr = reinterpret_cast<uint32_t>(&regs->DATA),
        .memory_addr = reinterpret_cast<uint32_t>(rx_buffer),
        .count = static_cast<uint16_t>(len),
        .direction = DmaDirection::PeriphToMemory,
        .periph_width = DmaWidth::Byte,
        .memory_width = DmaWidth::Byte,
        .priority = DmaPriority::High,
        .periph_inc = false,
        .memory_inc = rx != nullptr,
        .circular = false,
    };
    const DmaConfig tx_config = {
        .request_id = m_dma_tx.request_id,
        .periph_addr = reinterpret_cast<uint32_t>(&regs->DATA),
        .memory_addr = reinterpret_cast<uint32_t>(tx_buffer),
        .count = static_cast<uint16_t>(len),
        .direction = DmaDirection::MemoryToPeriph,
        .periph_width = DmaWidth::Byte,
        .memory_width = DmaWidth::Byte,
        .priority = DmaPriority::High,
        .periph_inc = false,
        .memory_inc = tx != nullptr,
        .circular = false,
    };
    Status status = rx_channel.config(rx_config);
    if (status != Status::Ok) {
        return status;
    }
    status = tx_channel.config(tx_config);
    if (status != Status::Ok) {
        return status;
    }
    return rx_channel.start();
}

Status SpiBase::wait_sync_dma(
    DmaChannel& tx_channel, DmaChannel& rx_channel,
    osal::Deadline& deadline)
{
    for (uint8_t completion = 0U; completion < 2U; ++completion) {
        if (m_xfer_sem.take(deadline.remaining()) != 0) {
            m_stats.timeout_count++;
            (void)tx_channel.stop();
            (void)rx_channel.stop();
            tx_channel.clear_flags();
            rx_channel.clear_flags();
            return Status::Timeout;
        }
    }
    if (m_dma_error_mask.load(std::memory_order_relaxed) != 0U
        || m_dma_done_mask.load(std::memory_order_relaxed) != 0x03U) {
        (void)tx_channel.stop();
        (void)rx_channel.stop();
        m_stats.error_count++;
        return Status::HardwareError;
    }
    auto* regs = reinterpret_cast<gd32::SpiRegs*>(m_base);
    while ((regs->STAT & STAT_TRANS) != 0U) {
        if (deadline.expired()) {
            m_stats.timeout_count++;
            return Status::Timeout;
        }
    }
    return Status::Ok;
}

Status SpiBase::transfer(const uint8_t *tx, uint8_t *rx, size_t len,
                         uint32_t timeout_ms, ChipSelect chip_select) {
    HAL_ASSERT_MSG(is_initialized(), "SPI not initialized");
    if (!is_initialized() || len == 0 || len > 0xFFFFU) {
        return Status::InvalidArgument;
    }
    if (m_async_reserved.load(std::memory_order_acquire)) {
        return Status::Busy;
    }

    osal::Deadline deadline(timeout_ms);
    osal::LockGuard lock(m_bus_mutex, deadline.remaining());
    if (!lock.owns_lock()) {
        return Status::Busy;
    }
    if (m_async_reserved.load(std::memory_order_acquire)) {
        return Status::Busy;
    }
    while (m_xfer_sem.take(0U) == 0) {
    }
    m_dma_done_mask = 0U;
    m_dma_error_mask = 0U;
    DmaChannel tx_dma(static_cast<uint32_t>(m_dma_tx.controller),
                      m_dma_tx.channel, m_dma_tx.mux_channel);
    DmaChannel rx_dma(static_cast<uint32_t>(m_dma_rx.controller),
                      m_dma_rx.channel, m_dma_rx.mux_channel);
    tx_dma.clear_flags();
    rx_dma.clear_flags();
    Status dma_status = configure_sync_dma(
        tx, rx, len, tx_dma, rx_dma);
    if (dma_status != Status::Ok) {
        m_stats.error_count++;
        return dma_status;
    }
    detail::SpiChipSelectGuard select_guard(
        chip_select.function, chip_select.argument);
    dma_status = tx_dma.start();
    if (dma_status != Status::Ok) {
        (void)rx_dma.stop();
        m_stats.error_count++;
        return dma_status;
    }
    dma_status = wait_sync_dma(tx_dma, rx_dma, deadline);
    if (dma_status != Status::Ok) {
        return dma_status;
    }
    m_stats.xfer_count++;
    m_stats.xfer_bytes += len;
    return Status::Ok;
}

Status SpiBase::begin_async(AsyncCallback callback, void *argument)
{
    if (!is_initialized() || callback == nullptr) {
        return Status::InvalidArgument;
    }
    osal::LockGuard lock(m_bus_mutex, 0U);
    if (!lock.owns_lock()) {
        return Status::Busy;
    }
    bool expected = false;
    if (!m_async_reserved.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return Status::Busy;
    }
    m_async_callback = callback;
    m_async_argument = argument;
    m_async_active.store(false, std::memory_order_release);
    return Status::Ok;
}

Status SpiBase::transfer_async(
    const uint8_t *tx, uint8_t *rx, size_t len,
    ChipSelect chip_select)
{
    if (!is_initialized() || !m_async_reserved.load(
            std::memory_order_acquire)
        || chip_select.function == nullptr
        || len == 0U || len > 0xFFFFU) {
        return Status::InvalidArgument;
    }
    bool expected = false;
    if (!m_async_active.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel)) {
        return Status::Busy;
    }

    m_dma_done_mask.store(0U, std::memory_order_relaxed);
    m_dma_error_mask.store(0U, std::memory_order_relaxed);
    m_async_chip_select = chip_select.function;
    m_async_chip_select_arg = chip_select.argument;
    auto *regs = reinterpret_cast<gd32::SpiRegs *>(m_base);
    const uint8_t *tx_buffer = tx != nullptr ? tx : &m_async_dummy_tx;
    uint8_t *rx_buffer = rx != nullptr ? rx : &m_async_dummy_rx;
    const DmaConfig rx_config = {
        .request_id = m_dma_rx.request_id,
        .periph_addr = reinterpret_cast<uint32_t>(&regs->DATA),
        .memory_addr = reinterpret_cast<uint32_t>(rx_buffer),
        .count = static_cast<uint16_t>(len),
        .direction = DmaDirection::PeriphToMemory,
        .periph_width = DmaWidth::Byte,
        .memory_width = DmaWidth::Byte,
        .priority = DmaPriority::VeryHigh,
        .periph_inc = false,
        .memory_inc = rx != nullptr,
        .circular = false,
    };
    const DmaConfig tx_config = {
        .request_id = m_dma_tx.request_id,
        .periph_addr = reinterpret_cast<uint32_t>(&regs->DATA),
        .memory_addr = reinterpret_cast<uint32_t>(tx_buffer),
        .count = static_cast<uint16_t>(len),
        .direction = DmaDirection::MemoryToPeriph,
        .periph_width = DmaWidth::Byte,
        .memory_width = DmaWidth::Byte,
        .priority = DmaPriority::VeryHigh,
        .periph_inc = false,
        .memory_inc = tx != nullptr,
        .circular = false,
    };
    if (m_async_rx_channel.config(rx_config) != Status::Ok
        || m_async_tx_channel.config(tx_config) != Status::Ok
        || m_async_rx_channel.start() != Status::Ok) {
        m_async_active.store(false, std::memory_order_release);
        return Status::HardwareError;
    }
    chip_select.function(chip_select.argument, true);
    if (m_async_tx_channel.start() != Status::Ok) {
        chip_select.function(chip_select.argument, false);
        (void)m_async_rx_channel.stop();
        m_async_active.store(false, std::memory_order_release);
        return Status::HardwareError;
    }
    return Status::Ok;
}

Status SpiBase::end_async()
{
    if (!m_async_reserved.load(std::memory_order_acquire)) {
        return Status::Ok;
    }
    hal::IrqGuard guard;
    (void)m_async_tx_channel.stop();
    (void)m_async_rx_channel.stop();
    if (m_async_active.exchange(false, std::memory_order_acq_rel)
        && m_async_chip_select != nullptr) {
        m_async_chip_select(m_async_chip_select_arg, false);
    }
    m_async_callback = nullptr;
    m_async_argument = nullptr;
    m_async_chip_select = nullptr;
    m_async_chip_select_arg = nullptr;
    m_async_reserved.store(false, std::memory_order_release);
    return Status::Ok;
}

void SpiBase::complete_async_from_isr(DmaCompletion completion)
{
    const uint8_t errors =
        m_dma_error_mask.load(std::memory_order_relaxed);
    if (errors == 0U && completion != DmaCompletion::Receive) {
        return;
    }
    if (!m_async_active.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    (void)m_async_tx_channel.stop();
    (void)m_async_rx_channel.stop();
    if (m_async_chip_select != nullptr) {
        m_async_chip_select(m_async_chip_select_arg, false);
    }
    if (m_async_callback != nullptr) {
        m_async_callback(
            m_async_argument,
            errors == 0U ? Status::Ok : Status::HardwareError);
    }
}

void SpiBase::dma_tx_isr(osal::IsrContext& context)
{
    if (!dma_channel_isr(m_dma_tx, m_dma_done_mask,
                         m_dma_error_mask, 0x01U)) {
        return;
    }
    if (m_async_reserved.load(std::memory_order_acquire)) {
        complete_async_from_isr(DmaCompletion::Transmit);
        return;
    }
    (void)m_xfer_sem.release_from_isr(context);
}

void SpiBase::dma_rx_isr(osal::IsrContext& context)
{
    if (!dma_channel_isr(m_dma_rx, m_dma_done_mask,
                         m_dma_error_mask, 0x02U)) {
        return;
    }
    if (m_async_reserved.load(std::memory_order_acquire)) {
        complete_async_from_isr(DmaCompletion::Receive);
        return;
    }
    (void)m_xfer_sem.release_from_isr(context);
}

} // namespace hal
