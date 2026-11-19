#include <drivers/adc.h>
#include <gd32_regs.h>
#include <device_adapters/gd32_timer_trigger.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#endif

void osal_interrupt_enter() {}
void osal_interrupt_leave() {}
void osal_yield_from_isr(int) {}

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    std::abort(); } } while (false)

struct Captured {
    unsigned count {0U};
    const uint16_t* data {nullptr};
    size_t size {0U};
    hal::Status status {hal::Status::Ok};
};

static void complete(void* argument, const uint16_t* data, size_t size, hal::Status status)
{
    auto& captured = *static_cast<Captured*>(argument);
    ++captured.count;
    captured.data = data;
    captured.size = size;
    captured.status = status;
}

int main()
{
    constexpr uintptr_t address = 0x40010000U;
    constexpr size_t size = 0x20000U;
#if defined(_WIN32)
    void* memory = VirtualAlloc(reinterpret_cast<void*>(address), size,
                                MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* memory = mmap(reinterpret_cast<void*>(address), size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    CHECK(memory == reinterpret_cast<void*>(address));
    std::memset(memory, 0, size);
    {
        using Current = hal::Gd32TimerTrigger<TIMER0_BASE, GD32_TIMER_COMPARE_CH3>;
        using Joint = hal::Gd32TimerTrigger<TIMER7_BASE, GD32_TIMER_UPDATE>;
        static_assert(Current::input == 14U && Joint::input == 15U);
        hal::Pwm<TIMER0_BASE> pwm;
        hal::Pwm<TIMER7_BASE> joint;
        hal::PwmConfig timer {};
        timer.period = 4800U;
        CHECK(pwm.init(timer) == hal::Status::Ok && joint.init(timer) == hal::Status::Ok);
        CHECK(Current::configure(pwm, 4801U) == hal::Status::InvalidArgument);
        CHECK(Current::configure(pwm, 4608U) == hal::Status::Ok);
        CHECK(Joint::configure(joint) == hal::Status::Ok);
        auto& timer0 = *reinterpret_cast<gd32::TimerRegs*>(TIMER0_BASE);
        auto& timer7 = *reinterpret_cast<gd32::TimerRegs*>(TIMER7_BASE);
        CHECK(timer0.CH3CV == 4608U && (timer0.CHCTL2 & (1U << 12U)));
        CHECK((timer0.CHCTL1 & (7U << 12U)) == (6U << 12U) && timer0.SWEVG == 1U);
        CHECK((timer7.CTL1 & (7U << 4U)) == (2U << 4U));
        CHECK(pwm.start() == hal::Status::Ok && Current::configure(pwm, 1U) == hal::Status::Busy);
        hal::Adc<ADC1_BASE> adc;
        hal::AdcConfig config;
        config.oversample_ratio = 16U;
        config.scan_dma = {DMA0_BASE, 66U, 1U, 1U};
        CHECK(adc.init(config) == hal::Status::Ok);
        auto& regs = *reinterpret_cast<gd32::AdcRegs*>(ADC1_BASE);
        auto& dma = *reinterpret_cast<gd32::DmaRegs*>(DMA0_BASE);
        CHECK((regs.OVSAMPCTL & 0x1FFU) == 13U);
        CHECK((gd32::rcu_apb2en() & (1U << 10U)) != 0U);
        hal::AdcScanConfig scan;
        scan.trigger_input = Joint::input;
        scan.channel_count = 2U;
        scan.channels[0] = {hal::AdcChannel::Ch17, hal::AdcSampleTime::Cycles16};
        scan.channels[1] = {hal::AdcChannel::Ch7, hal::AdcSampleTime::Cycles16};
        alignas(4) uint16_t buffer[4] {100U, 200U, 300U, 400U};
        Captured captured;
        CHECK(adc.start_scan(scan, buffer, 3U, complete, &captured) == hal::Status::InvalidArgument);
        CHECK(dma.CH[1].CNT == 0U);
        CHECK(adc.start_scan(scan, buffer, 4U, complete, &captured) == hal::Status::Ok);
        CHECK(regs.RSQ2 == (17U | (7U << 5U)));
        CHECK(regs.RSQ0 == (1U << 20U));
        CHECK(dma.CH[1].CNT == 4U);
        CHECK(dma.CH[1].PADDR == ADC1_BASE + 0x4CU);
        CHECK((*reinterpret_cast<uint32_t*>(0x40014440U) & 0xFFU) == 15U);
        uint16_t result;
        CHECK(adc.read(hal::AdcChannel::Ch0, result) == hal::Status::InvalidArgument);
        osal::IsrContext context;
        dma.INTF = 4U << 4U;
        adc.scan_dma_isr(context);
        CHECK(captured.count == 1U && captured.data == buffer && captured.size == 2U);
        dma.INTF = 2U << 4U;
        adc.scan_dma_isr(context);
        CHECK(captured.count == 2U && captured.data == buffer+2 && captured.status == hal::Status::Ok);
        dma.INTF = 6U << 4U;
        adc.scan_dma_isr(context);
        CHECK(captured.count == 3U && captured.data == nullptr && captured.status == hal::Status::HardwareError);
        CHECK((dma.CH[1].CTL & 1U) == 0U && (regs.CTL1 & (1U << 8U)) == 0U);
        adc.scan_dma_isr(context);
        CHECK(captured.count == 3U);
        CHECK(adc.start_scan(scan, buffer, 4U, complete, &captured) == hal::Status::Ok);
        CHECK(adc.stop_scan() == hal::Status::Ok);
        CHECK(adc.deinit() == hal::Status::Ok);
    }
#if defined(_WIN32)
    CHECK(VirtualFree(memory, 0U, MEM_RELEASE));
#else
    CHECK(munmap(memory, size) == 0);
#endif
}
