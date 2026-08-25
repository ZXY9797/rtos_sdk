#include <sensor_core.h>
#include <arch/arm/cortex_m/fault.h>
#include <irq.h>

SensorCore::SensorCore(const Config &cfg) : cfg_(cfg) {}

bool SensorCore::configure(const Config &cfg)
{
    if (thread_.load(std::memory_order_acquire) != nullptr
        || timer_attached_.load(std::memory_order_acquire)) {
        return false;
    }
    cfg_ = cfg;
    return true;
}

SensorCore::~SensorCore()
{
    if (stop() != 0) {
        // Continuing destruction would leave an ISR callback targeting freed
        // storage. Stop the system at the ownership boundary instead.
        hal::fault::panic(hal::fault::FatalReason::ThreadShutdownTimeout,
                          0, "sensor_core_stop", 0U);
    }
}

int SensorCore::start()
{
    if (thread_.load(std::memory_order_acquire) != nullptr
        || cfg_.entry == nullptr || cfg_.timer == nullptr
        || cfg_.frequency_hz == 0U || cfg_.divider == 0U) {
        return -1;
    }
    osal::PeriodicThreadConfig thread_config {};
    thread_config.name = cfg_.name;
    thread_config.entry = thread_entry;
    thread_config.context = this;
    thread_config.stack_buffer = cfg_.stack_buffer;
    thread_config.stack_size_bytes = cfg_.stack_size;
    thread_config.priority = cfg_.priority;
    thread_config.frequency_hz = cfg_.frequency_hz;
    thread_config.trigger = osal::PeriodicTrigger::External;
    thread_config.register_isr_trigger = false;
    osal::PeriodicThread *thread = &periodic_thread_;
    if (!thread->start(thread_config)) return -1;

    if (thread->startup() != 0) {
        thread->destroy();
        return -1;
    }

    thread_.store(thread, std::memory_order_release);
    if (!cfg_.timer->enable_update_irq(timer_callback, this)) {
        thread_.store(nullptr, std::memory_order_release);
        thread->destroy();
        return -1;
    }
    timer_attached_.store(true, std::memory_order_release);
    read_error_count_.store(0U, std::memory_order_relaxed);
    return 0;
}

int SensorCore::stop()
{
    osal::PeriodicThread *thread = nullptr;
    {
        hal::IrqGuard guard;
        if (timer_attached_.load(std::memory_order_acquire)
            && cfg_.timer != nullptr
            && !cfg_.timer->enable_update_irq(nullptr, nullptr)) {
            return -1;
        }
        timer_attached_.store(false, std::memory_order_release);
        thread = thread_.exchange(nullptr, std::memory_order_acq_rel);
    }
    if (thread != nullptr) {
        thread->destroy();
    }
    fire_count_.store(0U, std::memory_order_relaxed);
    return 0;
}

void SensorCore::timer_callback(void *arg)
{
    auto *self = static_cast<SensorCore *>(arg);
    self->on_sensor_done();
}

void SensorCore::thread_entry(
    void *arg, const osal::PeriodicStats& stats)
{
    auto *self = static_cast<SensorCore *>(arg);
    if (self->cfg_.read_fn
        && !self->cfg_.read_fn(self->cfg_.sensor_arg)) {
        self->read_error_count_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    if (self->cfg_.entry) {
        self->cfg_.entry(self->cfg_.param, stats);
    }
}

void SensorCore::on_sensor_done()
{
    uint32_t cnt = fire_count_.load(std::memory_order_relaxed) + 1U;
    if (cnt >= cfg_.divider) {
        cnt = 0;
        if (auto *thread = thread_.load(std::memory_order_acquire);
            thread != nullptr) {
            (void)thread->notify_from_isr();
        }
    }
    fire_count_.store(cnt, std::memory_order_relaxed);
}
