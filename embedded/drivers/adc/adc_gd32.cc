#include <drivers/adc.h>
#include <gd32_regs.h>

namespace hal {

using namespace gd32;

// ADC 寄存器位定义
// CTL0
constexpr uint32_t CTL0_ADCON   = (1U << 0);   // ADC 使能
constexpr uint32_t CTL0_ADCAL   = (1U << 1);   // 校准
constexpr uint32_t CTL0_SWRCST  = (1U << 22);  // 软件开始常规转换
// CTL1
constexpr uint32_t CTL1_ADCON   = (1U << 0);
constexpr uint32_t CTL1_CONVCNT_Pos = 13;
constexpr uint32_t CTL1_CONVCNT_Msk = (0xFU << 13);
constexpr uint32_t CTL1_ETSRCI_Pos = 17;
constexpr uint32_t CTL1_ETSRCI_Msk = (7U << 17);
constexpr uint32_t CTL1_ETSRCI_T0TRGO = (0U << 17);  // TIMER0 TRGO
constexpr uint32_t CTL1_ETEVTIC  = (1U << 20); // 外部触发注入通道
constexpr uint32_t CTL1_SWICST   = (1U << 22); // 软件开始注入转换
constexpr uint32_t CTL1_DISRC    = (1U << 11); // 常规通道 DMA
constexpr uint32_t CTL1_DISIC    = (1U << 12); // 注入通道 DMA
// ISQ
constexpr uint32_t ISQ_IL_Pos = 20;
constexpr uint32_t ISQ_IL_Msk = (3U << 20);
// RSQx — 通道序列
constexpr uint32_t RSQ_CH_Pos = 0;   // 通道号起始位
constexpr uint32_t RSQ_CH_Msk = 0x1F;
// SAMPTx — 采样时间 (3 bits/channel)
constexpr uint32_t SAMPT_BITS = 3;
// STAT
constexpr uint32_t STAT_EOC   = (1U << 1);
constexpr uint32_t STAT_EOIC  = (1U << 3); // 注入转换结束
constexpr uint32_t STAT_STRC  = (1U << 4); // 常规通道开始
constexpr uint32_t STAT_STIC  = (1U << 5); // 注入通道开始
// OVSMOD
constexpr uint32_t OVSMOD_OVSEN = (1U << 9); // 过采样使能

// ADC0 中断号 (GD32F503)
static constexpr int ADC0_1_IRQn = 18; // ADC0_1_IRQn

static AdcBase::IrqCallback s_adc0_eoc_cb = nullptr;
static void *s_adc0_eoc_arg = nullptr;

extern "C" void ADC0_1_IRQHandler() {
    auto *regs = reinterpret_cast<AdcRegs *>(ADC0_BASE);
    if (regs->CTL0 & (1U << 3)) { // EOIC flag
        // 清除注入完成标志
        regs->CTL0 &= ~(1U << 3);
        if (s_adc0_eoc_cb) {
            s_adc0_eoc_cb(s_adc0_eoc_arg);
        }
    }
}

Status AdcBase::init(const AdcConfig &cfg) {
    auto *regs = reinterpret_cast<AdcRegs *>(m_base);

    // 使能 ADC 时钟
    rcu_apb2en() |= clk::ADC0EN;

    // 配置分辨率
    uint32_t ctl1 = 0;
    switch (cfg.resolution) {
    case 6:  ctl1 |= (3U << 3); break; // RES=11
    case 8:  ctl1 |= (2U << 3); break; // RES=10
    case 10: ctl1 |= (1U << 3); break; // RES=01
    default: break;                      // RES=00 → 12-bit
    }
    regs->CTL1 = ctl1;

    // 使能 ADC
    regs->CTL0 |= CTL0_ADCON;

    // 校准 (带超时)
    regs->CTL0 |= CTL0_ADCAL;
    constexpr uint32_t CAL_TIMEOUT = 1000000U;
    uint32_t cal_wait = CAL_TIMEOUT;
    while ((regs->CTL0 & CTL0_ADCAL) && cal_wait > 0) {
        cal_wait--;
    }
    if (cal_wait == 0) return Status::Timeout;

    m_initialized = true;
    return Status::Ok;
}

Status AdcBase::deinit() {
    if (!m_initialized) return Status::Ok;
    auto *regs = reinterpret_cast<AdcRegs *>(m_base);
    regs->CTL0 &= ~CTL0_ADCON;
    m_initialized = false;
    return Status::Ok;
}

Status AdcBase::read(AdcChannel ch, uint16_t &value) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<AdcRegs *>(m_base);

    // 配置常规通道 0
    regs->RSQ2 = (static_cast<uint32_t>(ch) & RSQ_CH_Msk) << RSQ_CH_Pos;

    // 清除 EOC
    regs->CTL0 &= ~(1U << 1);

    // 启动转换
    regs->CTL0 |= CTL0_SWRCST;

    // 等待完成
    while (!(regs->CTL0 & (1U << 1))) {}
    value = static_cast<uint16_t>(regs->RDATA & 0xFFF);

    return Status::Ok;
}

Status AdcBase::config_injected(const AdcInjectedConfig &cfg) {
    if (!m_initialized) return Status::InvalidArgument;
    if (cfg.channel_count == 0 || cfg.channel_count > 4) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<AdcRegs *>(m_base);

    // 设置注入通道数 (IL = channel_count - 1)
    regs->ISQ = ((cfg.channel_count - 1) << ISQ_IL_Pos) & ISQ_IL_Msk;

    // 配置注入通道序列 — ISQ 寄存器逆序写入
    // rank 0 在高位, rank 3 在低位
    for (uint16_t i = 0; i < cfg.channel_count; i++) {
        uint32_t shift = (3 - i) * 5;
        regs->ISQ = (regs->ISQ & ~(RSQ_CH_Msk << shift))
                   | ((static_cast<uint32_t>(cfg.channels[i].channel) & RSQ_CH_Msk) << shift);

        // 设置采样时间
        uint32_t ch = static_cast<uint32_t>(cfg.channels[i].channel);
        if (ch < 10) {
            regs->SAMPT0 = (regs->SAMPT0 & ~(7U << (ch * SAMPT_BITS)))
                          | (static_cast<uint32_t>(cfg.channels[i].sample_time) << (ch * SAMPT_BITS));
        } else {
            regs->SAMPT1 = (regs->SAMPT1 & ~(7U << ((ch - 10) * SAMPT_BITS)))
                          | (static_cast<uint32_t>(cfg.channels[i].sample_time) << ((ch - 10) * SAMPT_BITS));
        }
    }

    // 配置外部触发源 (TIMER0 TRGO)
    regs->CTL1 = (regs->CTL1 & ~CTL1_ETSRCI_Msk) | CTL1_ETSRCI_T0TRGO;

    // 使能注入通道外部触发
    regs->CTL1 |= CTL1_ETEVTIC;

    return Status::Ok;
}

Status AdcBase::start_injected() {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<AdcRegs *>(m_base);

    // 清除注入完成标志
    regs->CTL0 &= ~(1U << 3);

    // 软件触发注入转换
    regs->CTL1 |= CTL1_SWICST;

    return Status::Ok;
}

Status AdcBase::read_injected(uint8_t rank, uint16_t &value) {
    if (!m_initialized || rank > 3) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<AdcRegs *>(m_base);

    volatile uint32_t *data_regs[] = {
        &regs->IDATA0, &regs->IDATA1, &regs->IDATA2, &regs->IDATA3
    };
    value = static_cast<uint16_t>(*data_regs[rank] & 0xFFF);

    return Status::Ok;
}

Status AdcBase::config_watchdog(AdcChannel ch, uint16_t high_threshold, uint16_t low_threshold) {
    if (!m_initialized) return Status::InvalidArgument;
    auto *regs = reinterpret_cast<AdcRegs *>(m_base);

    regs->WDHIGH = high_threshold & 0xFFF;
    regs->WDLOW = low_threshold & 0xFFF;

    // 设置看门狗通道 (CTL0 WDL_CH 位)
    uint32_t ch_val = static_cast<uint32_t>(ch);
    regs->CTL0 = (regs->CTL0 & ~(0x1FU << 9)) | ((ch_val & 0x1F) << 9);
    // 使能看门狗
    regs->CTL0 |= (1U << 24); // RWDEN

    return Status::Ok;
}

Status AdcBase::set_eoc_callback(IrqCallback cb, void *arg) {
    m_eoc_cb = cb;
    m_eoc_arg = arg;

    if (m_base != ADC0_BASE) return Status::NotSupported;

    s_adc0_eoc_cb = cb;
    s_adc0_eoc_arg = arg;

    if (cb) {
        // 使能注入完成中断
        auto *regs = reinterpret_cast<AdcRegs *>(m_base);
        regs->CTL0 |= (1U << 8); // EOICIE
    }
    return Status::Ok;
}

} // namespace hal
