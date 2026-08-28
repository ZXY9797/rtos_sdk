#include <sensor_core.h>
#include <irq.h>

#include <cstddef>
#include <cstdint>

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

bool SensorBatchCore::valid_config(const Config& config) const
{
    if (config.timer == nullptr || config.entry == nullptr
        || config.source_count == 0U
        || config.source_count > kMaxSources
        || config.buffers[0] == nullptr || config.buffers[1] == nullptr
        || config.buffers[0] == config.buffers[1]
        || config.frame_size < sizeof(FrameHeader)
        || config.buffer_capacity == 0U
        || config.frame_size > SIZE_MAX / config.buffer_capacity
        || config.buffer_bytes
            < config.frame_size * config.buffer_capacity
        || config.frame_size % alignof(FrameHeader) != 0U
        || reinterpret_cast<uintptr_t>(config.buffers[0])
            % alignof(FrameHeader) != 0U
        || reinterpret_cast<uintptr_t>(config.buffers[1])
            % alignof(FrameHeader) != 0U
        || config.sample_frequency_hz == 0U
        || config.consumer_frequency_hz == 0U
        || config.sample_frequency_hz % config.consumer_frequency_hz != 0U
        || config.stack_buffer == nullptr || config.stack_size == 0U
        || reinterpret_cast<uintptr_t>(config.stack_buffer)
            % alignof(std::max_align_t) != 0U
        || config.priority > osal::kPriorityMax) {
        return false;
    }
    const uint32_t batch_size = config.sample_frequency_hz
        / config.consumer_frequency_hz;
    if (batch_size == 0U || batch_size > config.buffer_capacity
        || batch_size > UINT16_MAX) {
        return false;
    }
    for (size_t index = 0U; index < config.source_count; ++index) {
        const Source& source = config.sources[index];
        if (source.prepare == nullptr || source.start == nullptr
            || source.stop == nullptr || source.frame_bytes == 0U
            || source.frame_offset < sizeof(FrameHeader)
            || source.frame_offset > config.frame_size
            || source.frame_bytes
                > config.frame_size - source.frame_offset) {
            return false;
        }
    }
    for (size_t left = 0U; left < config.source_count; ++left) {
        const Source& source = config.sources[left];
        for (size_t right_index = left + 1U;
             right_index < config.source_count; ++right_index) {
            const Source& right = config.sources[right_index];
            const size_t source_end =
                source.frame_offset + source.frame_bytes;
            const size_t right_end =
                right.frame_offset + right.frame_bytes;
            if (source.frame_offset < right_end
                && right.frame_offset < source_end) {
                return false;
            }
        }
    }
    return true;
}

bool SensorBatchCore::configure(const Config& config)
{
    if (running_.load(std::memory_order_acquire)
        || timer_attached_.load(std::memory_order_acquire)
        || !valid_config(config)) {
        return false;
    }
    config_ = config;
    frames_per_batch_ = config.sample_frequency_hz
        / config.consumer_frequency_hz;
    expected_source_mask_ = (1U << config.source_count) - 1U;
    timestamp_step_us_ = 1000000U / config.sample_frequency_hz;
    timestamp_remainder_ = 1000000U % config.sample_frequency_hz;
    return true;
}

bool SensorBatchCore::prepare_sources()
{
    prepared_source_count_ = 0U;
    for (size_t index = 0U; index < config_.source_count; ++index) {
        tokens_[index] = {this, static_cast<uint8_t>(index)};
        const Source& source = config_.sources[index];
        if (!source.prepare(source.context, source_callback,
                            &tokens_[index])) {
            stop_sources();
            return false;
        }
        prepared_source_count_++;
    }
    return true;
}

void SensorBatchCore::stop_sources()
{
    while (prepared_source_count_ > 0U) {
        prepared_source_count_--;
        const Source& source =
            config_.sources[prepared_source_count_];
        source.stop(source.context);
    }
}

void SensorBatchCore::reset_runtime_state()
{
    for (BufferSlot& slot : slots_) {
        slot.frame_count = 0U;
        slot.first_sequence = 0U;
        slot.first_capture_time_us = 0U;
        slot.last_capture_time_us = 0U;
        slot.state.store(BufferState::Free, std::memory_order_relaxed);
    }
    slots_[0].state.store(BufferState::Filling,
                          std::memory_order_relaxed);
    fill_buffer_.store(0U, std::memory_order_relaxed);
    active_buffer_.store(kInvalidBuffer, std::memory_order_relaxed);
    active_frame_.store(0U, std::memory_order_relaxed);
    ready_mask_.store(0U, std::memory_order_relaxed);
    completion_mask_.store(0U, std::memory_order_relaxed);
    error_mask_.store(0U, std::memory_order_relaxed);
    transfer_active_.store(false, std::memory_order_relaxed);
    trigger_count_.store(0U, std::memory_order_relaxed);
    sample_count_.store(0U, std::memory_order_relaxed);
    source_error_count_.store(0U, std::memory_order_relaxed);
    trigger_overrun_count_.store(0U, std::memory_order_relaxed);
    batch_overrun_count_.store(0U, std::memory_order_relaxed);
    dispatch_error_count_.store(0U, std::memory_order_relaxed);
    timestamp_phase_ = 0U;
    next_capture_time_us_ = osal::Kernel::uptime_ms() * 1000U;
}

uint8_t SensorBatchCore::claim_free_buffer()
{
    const uint8_t current =
        fill_buffer_.load(std::memory_order_acquire);
    if (current < kBufferCount
        && slots_[current].state.load(std::memory_order_acquire)
            == BufferState::Filling) {
        return current;
    }
    for (uint8_t index = 0U; index < kBufferCount; ++index) {
        BufferState expected = BufferState::Free;
        if (slots_[index].state.compare_exchange_strong(
                expected, BufferState::Filling,
                std::memory_order_acq_rel)) {
            slots_[index].frame_count = 0U;
            fill_buffer_.store(index, std::memory_order_release);
            return index;
        }
    }
    return kInvalidBuffer;
}

SensorBatchCore::FrameHeader*
SensorBatchCore::active_frame_header() const
{
    const uint8_t buffer =
        active_buffer_.load(std::memory_order_acquire);
    const uint16_t frame =
        active_frame_.load(std::memory_order_acquire);
    if (buffer >= kBufferCount || frame >= config_.buffer_capacity) {
        return nullptr;
    }
    auto* bytes = static_cast<std::byte*>(config_.buffers[buffer]);
    return reinterpret_cast<FrameHeader*>(
        bytes + static_cast<size_t>(frame) * config_.frame_size);
}

int SensorBatchCore::start()
{
    if (running_.load(std::memory_order_acquire)
        || frames_per_batch_ == 0U || !prepare_sources()) {
        return -1;
    }
    reset_runtime_state();
    osal::PeriodicThreadConfig thread_config {};
    thread_config.name = config_.name;
    thread_config.entry = thread_entry;
    thread_config.context = this;
    thread_config.stack_buffer = config_.stack_buffer;
    thread_config.stack_size_bytes = config_.stack_size;
    thread_config.priority = config_.priority;
    thread_config.frequency_hz = config_.consumer_frequency_hz;
    thread_config.trigger = osal::PeriodicTrigger::External;
    thread_config.register_isr_trigger = false;
    if (!consumer_thread_.start(thread_config)
        || consumer_thread_.startup() != 0) {
        consumer_thread_.destroy();
        stop_sources();
        return -1;
    }
    running_.store(true, std::memory_order_release);
    if (!config_.timer->enable_update_irq(timer_callback, this)) {
        running_.store(false, std::memory_order_release);
        consumer_thread_.destroy();
        stop_sources();
        return -1;
    }
    timer_attached_.store(true, std::memory_order_release);
    return 0;
}

int SensorBatchCore::stop()
{
    if (timer_attached_.load(std::memory_order_acquire)) {
        hal::IrqGuard guard;
        running_.store(false, std::memory_order_release);
        if (!config_.timer->enable_update_irq(nullptr, nullptr)) {
            return -1;
        }
        timer_attached_.store(false, std::memory_order_release);
    } else {
        running_.store(false, std::memory_order_release);
    }
    stop_sources();
    transfer_active_.store(false, std::memory_order_release);
    consumer_thread_.destroy();
    return 0;
}

void SensorBatchCore::timer_callback(void* argument)
{
    auto* self = static_cast<SensorBatchCore*>(argument);
    self->on_timer_trigger();
}

void SensorBatchCore::source_callback(
    void* argument, SourceResult result)
{
    auto* token = static_cast<CompletionToken*>(argument);
    token->owner->on_source_complete(token->source_index, result);
}

void SensorBatchCore::on_timer_trigger()
{
    const uint32_t sequence =
        trigger_count_.fetch_add(1U, std::memory_order_relaxed) + 1U;
    const uint32_t capture_time = next_capture_time_us_;
    next_capture_time_us_ += timestamp_step_us_;
    timestamp_phase_ += timestamp_remainder_;
    if (timestamp_phase_ >= config_.sample_frequency_hz) {
        timestamp_phase_ -= config_.sample_frequency_hz;
        next_capture_time_us_++;
    }
    if (!running_.load(std::memory_order_acquire)
        || transfer_active_.load(std::memory_order_acquire)) {
        trigger_overrun_count_.fetch_add(1U,
                                         std::memory_order_relaxed);
        return;
    }
    const uint8_t buffer = claim_free_buffer();
    if (buffer == kInvalidBuffer) {
        batch_overrun_count_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    BufferSlot& slot = slots_[buffer];
    const uint16_t frame = slot.frame_count;
    active_buffer_.store(buffer, std::memory_order_relaxed);
    active_frame_.store(frame, std::memory_order_relaxed);
    FrameHeader* const header = active_frame_header();
    if (header == nullptr) {
        dispatch_error_count_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    header->sequence = sequence;
    header->capture_time_us = capture_time;
    header->valid_sources = 0U;
    completion_mask_.store(0U, std::memory_order_relaxed);
    error_mask_.store(0U, std::memory_order_relaxed);
    transfer_active_.store(true, std::memory_order_release);
    auto* frame_bytes = reinterpret_cast<std::byte*>(header);
    for (uint8_t index = 0U; index < config_.source_count; ++index) {
        const Source& source = config_.sources[index];
        if (!source.start(source.context,
                          frame_bytes + source.frame_offset)) {
            const uint8_t bit = static_cast<uint8_t>(1U << index);
            error_mask_.fetch_or(bit, std::memory_order_relaxed);
            completion_mask_.fetch_or(bit, std::memory_order_relaxed);
        }
    }
    if (completion_mask_.load(std::memory_order_acquire)
        == expected_source_mask_) {
        finalize_active_frame();
    }
}

void SensorBatchCore::on_source_complete(
    uint8_t source_index, SourceResult result)
{
    if (!running_.load(std::memory_order_acquire)
        || !transfer_active_.load(std::memory_order_acquire)
        || source_index >= config_.source_count) {
        return;
    }
    const uint8_t bit = static_cast<uint8_t>(1U << source_index);
    if (result != SourceResult::Success) {
        error_mask_.fetch_or(bit, std::memory_order_relaxed);
    }
    const uint8_t previous =
        completion_mask_.fetch_or(bit, std::memory_order_acq_rel);
    if ((previous & bit) != 0U) {
        source_error_count_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    if (static_cast<uint8_t>(previous | bit)
        == expected_source_mask_) {
        finalize_active_frame();
    }
}

void SensorBatchCore::finalize_active_frame()
{
    bool expected = true;
    if (!transfer_active_.compare_exchange_strong(
            expected, false, std::memory_order_acq_rel)) {
        return;
    }
    FrameHeader* const header = active_frame_header();
    const uint8_t buffer =
        active_buffer_.load(std::memory_order_relaxed);
    if (header == nullptr || buffer >= kBufferCount) {
        dispatch_error_count_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    const uint8_t errors = error_mask_.load(std::memory_order_relaxed);
    header->valid_sources = expected_source_mask_ & ~errors;
    for (uint8_t bit = 1U; bit <= expected_source_mask_; bit <<= 1U) {
        if ((errors & bit) != 0U) {
            source_error_count_.fetch_add(1U,
                                          std::memory_order_relaxed);
        }
    }
    BufferSlot& slot = slots_[buffer];
    if (slot.frame_count == 0U) {
        slot.first_sequence = header->sequence;
        slot.first_capture_time_us = header->capture_time_us;
    }
    slot.last_capture_time_us = header->capture_time_us;
    slot.frame_count++;
    sample_count_.fetch_add(1U, std::memory_order_relaxed);
    if (slot.frame_count >= frames_per_batch_) {
        dispatch_full_buffer(buffer);
    }
}

void SensorBatchCore::dispatch_full_buffer(uint8_t buffer_index)
{
    BufferSlot& slot = slots_[buffer_index];
    if (ready_mask_.load(std::memory_order_acquire) != 0U) {
        batch_overrun_count_.fetch_add(1U, std::memory_order_relaxed);
        slot.frame_count = 0U;
        return;
    }
    slot.state.store(BufferState::Ready, std::memory_order_release);
    ready_mask_.store(
        static_cast<uint8_t>(1U << buffer_index),
        std::memory_order_release);
    fill_buffer_.store(kInvalidBuffer, std::memory_order_release);
    (void)claim_free_buffer();
    if (consumer_thread_.notify_from_isr() != 0) {
        dispatch_error_count_.fetch_add(1U, std::memory_order_relaxed);
    }
}

void SensorBatchCore::thread_entry(
    void* argument, const osal::PeriodicStats&)
{
    static_cast<SensorBatchCore*>(argument)->consume_ready_batch();
}

void SensorBatchCore::consume_ready_batch()
{
    const uint8_t ready =
        ready_mask_.exchange(0U, std::memory_order_acq_rel);
    if (ready == 0U) {
        dispatch_error_count_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    const uint8_t index = (ready & 0x01U) != 0U ? 0U : 1U;
    BufferState expected = BufferState::Ready;
    if (!slots_[index].state.compare_exchange_strong(
            expected, BufferState::Reading,
            std::memory_order_acq_rel)) {
        dispatch_error_count_.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    const BufferSlot& slot = slots_[index];
    const BatchView batch {
        config_.buffers[index], slot.frame_count, config_.frame_size,
        slot.first_sequence, slot.first_capture_time_us,
        slot.last_capture_time_us,
    };
    config_.entry(config_.entry_context, batch);
    slots_[index].frame_count = 0U;
    slots_[index].state.store(BufferState::Free,
                              std::memory_order_release);
}

SensorBatchCore::Diagnostics SensorBatchCore::diagnostics() const
{
    return {
        trigger_count_.load(std::memory_order_relaxed),
        sample_count_.load(std::memory_order_relaxed),
        source_error_count_.load(std::memory_order_relaxed),
        trigger_overrun_count_.load(std::memory_order_relaxed),
        batch_overrun_count_.load(std::memory_order_relaxed),
        dispatch_error_count_.load(std::memory_order_relaxed),
        consumer_thread_.missed(),
    };
}
