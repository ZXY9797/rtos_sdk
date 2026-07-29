#include <sensor_core.h>

SensorCore::SensorCore(const Config &cfg) : cfg_(cfg) {}

SensorCore::~SensorCore()
{
    if (cfg_.timer) {
        cfg_.timer->enable_update_irq(nullptr, nullptr);
    }
    delete thread_;
}

int SensorCore::start()
{
    thread_ = osal::PeriodicThread::create(cfg_.name, thread_entry, this,
                                           cfg_.stack_size, cfg_.priority,
                                           cfg_.frequency_hz,
                                           osal::PeriodicTrigger::External);
    if (!thread_) return -1;

    if (cfg_.timer) {
        cfg_.timer->enable_update_irq(timer_callback, this);
    }

    return thread_->startup();
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
    if (self->cfg_.read_fn) {
        self->cfg_.read_fn(self->cfg_.sensor_arg);
    }
    if (self->cfg_.entry) {
        self->cfg_.entry(self->cfg_.param, stats);
    }
}

void SensorCore::on_sensor_done()
{
    uint32_t cnt = fire_count_ + 1;
    if (cnt >= cfg_.divider) {
        cnt = 0;
        if (thread_) {
            (void)thread_->notify_from_isr();
        }
    }
    fire_count_ = cnt;
}
