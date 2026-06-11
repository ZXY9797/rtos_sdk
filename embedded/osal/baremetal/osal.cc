#include <osal.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

struct osal_bare_sem {
    volatile uint32_t count;
    uint32_t max_count;
    bool used;
};

struct osal_bare_mutex {
    volatile bool locked;
    bool used;
};

namespace {

constexpr size_t kMaxSemaphores = 16;
constexpr size_t kMaxMutexes = 8;
constexpr size_t kMaxStreams = 8;

osal_bare_sem g_sems[kMaxSemaphores];
osal_bare_mutex g_mutexes[kMaxMutexes];
volatile uint32_t g_ticks;

struct StreamState {
    uint8_t *buf;
    size_t size;
    volatile size_t head;
    volatile size_t tail;
    volatile size_t count;
    bool used;
};

StreamState g_streams[kMaxStreams];

template <typename T, size_t N>
T *alloc_slot(T (&pool)[N]) {
    for (auto &slot : pool) {
        if (!slot.used) {
            std::memset(&slot, 0, sizeof(slot));
            slot.used = true;
            return &slot;
        }
    }
    return nullptr;
}

template <typename T>
void free_slot(T *slot) {
    if (slot) std::memset(slot, 0, sizeof(*slot));
}

void irq_lock() {
    asm volatile("cpsid i" ::: "memory");
}

void irq_unlock() {
    asm volatile("cpsie i" ::: "memory");
}

bool wait_expired(uint32_t start, uint32_t timeout_ms) {
    if (timeout_ms == OSAL_WAITING_FOREVER) return false;
    if (timeout_ms == 0U) return true;
    return (g_ticks - start) >= timeout_ms;
}

} // namespace

int osal_sleep(int ms) {
    for (int i = 0; i < ms * 1000; ++i) {
        asm volatile("nop");
    }
    return 0;
}

int osal_thread_yield(void) {
    return 0;
}

void osal_interrupt_enter(void) {
}

void osal_interrupt_leave(void) {
}

void sys_clock_announce(uint32_t ticks) {
    g_ticks += ticks;
}

int osal_init(void) {
    return 0;
}

int osal_start(void (*entry)(void)) {
    if (entry) entry();
    return 0;
}

namespace osal {

void Kernel::start() {
}

bool Kernel::is_running() {
    return true;
}

Kernel::SchedulerState Kernel::get_scheduler_state() {
    return SchedulerState::Running;
}

bool Kernel::in_isr() {
    return false;
}

uint32_t Kernel::tick_count() {
    g_ticks = g_ticks + 1U;
    return g_ticks;
}

uint32_t Kernel::uptime_ms() {
    return tick_count();
}

void Kernel::suspend_scheduler() {
    irq_lock();
}

bool Kernel::resume_scheduler() {
    irq_unlock();
    return true;
}

Semaphore::Semaphore(uint32_t initial, uint32_t max_count) {
    if (max_count == 0U || initial > max_count) return;
    handle_ = alloc_slot(g_sems);
    if (!handle_) return;
    handle_->count = initial;
    handle_->max_count = max_count;
    max_count_ = max_count;
}

Semaphore::~Semaphore() {
    free_slot(handle_);
    handle_ = nullptr;
    max_count_ = 0;
}

int Semaphore::take(Milliseconds timeout_ms) {
    if (!handle_) return -1;
    const uint32_t start = Kernel::tick_count();
    while (true) {
        irq_lock();
        if (handle_->count > 0U) {
            handle_->count = handle_->count - 1U;
            irq_unlock();
            return 0;
        }
        irq_unlock();

        if (wait_expired(start, timeout_ms)) return -1;
    }
}

int Semaphore::release() {
    if (!handle_) return -1;
    irq_lock();
    if (handle_->count < handle_->max_count) {
        handle_->count = handle_->count + 1U;
        irq_unlock();
        return 0;
    }
    irq_unlock();
    return -1;
}

uint32_t Semaphore::count() const {
    return handle_ ? handle_->count : 0U;
}

Mutex::Mutex() {
    (void)create();
}

Mutex::~Mutex() {
    destroy();
}

bool Mutex::create() {
    if (handle_) return true;
    handle_ = alloc_slot(g_mutexes);
    return handle_ != nullptr;
}

void Mutex::destroy() {
    free_slot(handle_);
    handle_ = nullptr;
}

int Mutex::lock(Milliseconds timeout_ms) {
    if (!handle_) return -1;
    const uint32_t start = Kernel::tick_count();
    while (true) {
        irq_lock();
        if (!handle_->locked) {
            handle_->locked = true;
            irq_unlock();
            return 0;
        }
        irq_unlock();
        if (wait_expired(start, timeout_ms)) return -1;
    }
}

int Mutex::try_lock() {
    return lock(0);
}

int Mutex::unlock() {
    if (!handle_) return -1;
    irq_lock();
    handle_->locked = false;
    irq_unlock();
    return 0;
}

Thread::Thread() = default;

Thread::~Thread() {
    destroy();
}

Thread *Thread::create(const char *, Entry, void *, size_t, int32_t, int32_t) {
    return nullptr;
}

bool Thread::start(Entry, void *, const ThreadConfig &) {
    return false;
}

void Thread::destroy() {
    handle_.handle = nullptr;
    handle_.flags = 0;
}

int Thread::startup() { return -1; }
int Thread::suspend() { return -1; }
int Thread::resume() { return -1; }
int Thread::yield() { return 0; }
bool Thread::set_priority(Priority) { return false; }
Priority Thread::get_priority() const { return kPriorityMin; }
Thread::State Thread::get_state() const { return State::Invalid; }
const char *Thread::get_name() const { return nullptr; }
bool Thread::abort_delay() { return false; }

IsrTrigger::Slot IsrTrigger::s_slots[kMaxSlots];
int IsrTrigger::s_count = 0;

int IsrTrigger::register_slot(Callback cb, void *arg) {
    if (!cb || s_count >= kMaxSlots) return -1;
    const int id = s_count++;
    s_slots[id] = {cb, arg};
    return id;
}

void IsrTrigger::fire(int id) {
    if (id >= 0 && id < s_count && s_slots[id].cb) {
        s_slots[id].cb(s_slots[id].arg);
    }
}

PeriodicThread::PeriodicThread(PrivateTag) {
}

PeriodicThread::~PeriodicThread() = default;

uint32_t PeriodicThread::nextDelayTicks(uint32_t, uint32_t, uint32_t &) {
    return 1;
}

void PeriodicThread::callEntry() {
    if (!entry_) return;
    sequence_ = sequence_ + 1U;
    PeriodicStats stats{sequence_, missed_};
    entry_(param_, stats);
}

void PeriodicThread::timer_isr_callback(void *arg) {
    auto *self = static_cast<PeriodicThread *>(arg);
    if (self) (void)self->notify_from_isr();
}

void PeriodicThread::threadEntry(void *) {
}

PeriodicThread *PeriodicThread::create(const char *, PeriodicEntry, void *,
                                       size_t, int32_t, uint32_t,
                                       PeriodicTrigger, IrqTimer *) {
    return nullptr;
}

int PeriodicThread::startup() { return -1; }
int PeriodicThread::stop() { return 0; }
int PeriodicThread::notify_from_isr(uint32_t) { return -1; }

EventGroup::~EventGroup() {
    destroy();
}

bool EventGroup::create() {
    native_handle_ = this;
    return true;
}

void EventGroup::destroy() {
    native_handle_ = nullptr;
}

uint32_t EventGroup::set_bits(uint32_t bits) { return bits; }
uint32_t EventGroup::clear_bits(uint32_t) { return 0; }
uint32_t EventGroup::get_bits() const { return 0; }
uint32_t EventGroup::wait_bits(uint32_t, bool, WaitMode, Milliseconds) { return 0; }
uint32_t EventGroup::sync(uint32_t, uint32_t, Milliseconds) { return 0; }

MessageQueue::~MessageQueue() {
    destroy();
}

bool MessageQueue::create(uint32_t, size_t item_size, void *, size_t) {
    item_size_ = item_size;
    native_handle_ = this;
    return true;
}

void MessageQueue::destroy() {
    native_handle_ = nullptr;
    item_size_ = 0;
}

bool MessageQueue::send(const void *, Milliseconds) { return false; }
bool MessageQueue::receive(void *, Milliseconds) { return false; }
bool MessageQueue::reset() { return true; }
uint32_t MessageQueue::count() const { return 0; }
uint32_t MessageQueue::free_slots() const { return 0; }

StreamBuffer::~StreamBuffer() {
    destroy();
}

bool StreamBuffer::create(size_t buf_size, size_t) {
    auto *buf = static_cast<uint8_t *>(rtos_malloc(buf_size));
    if (!buf) return false;
    return create(buf, buf_size, 1);
}

bool StreamBuffer::create(uint8_t *storage, size_t storage_size, size_t) {
    destroy();
    if (!storage || storage_size == 0) return false;

    auto *state = alloc_slot(g_streams);
    if (!state) return false;

    state->buf = storage;
    state->size = storage_size;
    handle_ = state;
    control_block_ = state;
    owns_storage_ = false;
    return true;
}

void StreamBuffer::destroy() {
    auto *state = static_cast<StreamState *>(control_block_);
    free_slot(state);
    handle_ = nullptr;
    control_block_ = nullptr;
    owns_storage_ = false;
}

size_t StreamBuffer::send(const uint8_t *data, size_t len, Milliseconds) {
    return send_from_isr(data, len, nullptr);
}

size_t StreamBuffer::send_from_isr(const uint8_t *data, size_t len,
                                   int *higher_prio_woken) {
    auto *state = static_cast<StreamState *>(control_block_);
    if (!state || !data || len == 0) return 0;

    size_t written = 0;
    irq_lock();
    while (written < len && state->count < state->size) {
        state->buf[state->head] = data[written++];
        state->head = (state->head + 1U) % state->size;
        state->count = state->count + 1U;
    }
    irq_unlock();

    if (higher_prio_woken) *higher_prio_woken = 0;
    return written;
}

size_t StreamBuffer::receive(uint8_t *data, size_t len,
                             Milliseconds timeout_ms) {
    auto *state = static_cast<StreamState *>(control_block_);
    if (!state || !data || len == 0) return 0;

    const uint32_t start = Kernel::tick_count();
    while (state->count == 0U) {
        if (wait_expired(start, timeout_ms)) return 0;
    }

    return receive_from_isr(data, len, nullptr);
}

size_t StreamBuffer::receive_from_isr(uint8_t *data, size_t len,
                                      int *higher_prio_woken) {
    auto *state = static_cast<StreamState *>(control_block_);
    if (!state || !data || len == 0) return 0;

    size_t read = 0;
    irq_lock();
    while (read < len && state->count > 0U) {
        data[read++] = state->buf[state->tail];
        state->tail = (state->tail + 1U) % state->size;
        state->count = state->count - 1U;
    }
    irq_unlock();

    if (higher_prio_woken) *higher_prio_woken = 0;
    return read;
}

size_t StreamBuffer::bytes_available() const {
    auto *state = static_cast<StreamState *>(control_block_);
    return state ? state->count : 0U;
}

size_t StreamBuffer::space_available() const {
    auto *state = static_cast<StreamState *>(control_block_);
    return state ? state->size - state->count : 0U;
}

void StreamBuffer::reset() {
    auto *state = static_cast<StreamState *>(control_block_);
    if (!state) return;
    irq_lock();
    state->head = 0;
    state->tail = 0;
    state->count = 0;
    irq_unlock();
}

void SoftTimer::dispatch(void *timer) {
    auto *self = static_cast<SoftTimer *>(timer);
    if (self && self->callback_) self->callback_(self->context_);
}

SoftTimer::~SoftTimer() {
    destroy();
}

bool SoftTimer::create(const char *, Milliseconds, bool,
                       Callback callback, void *context) {
    callback_ = callback;
    context_ = context;
    native_handle_ = callback ? this : nullptr;
    return native_handle_ != nullptr;
}

void SoftTimer::destroy() {
    native_handle_ = nullptr;
    callback_ = nullptr;
    context_ = nullptr;
}

bool SoftTimer::start() { return native_handle_ != nullptr; }
bool SoftTimer::stop() { return native_handle_ != nullptr; }
bool SoftTimer::reset() { return native_handle_ != nullptr; }
bool SoftTimer::change_period(Milliseconds) { return native_handle_ != nullptr; }
bool SoftTimer::is_active() const { return native_handle_ != nullptr; }

CriticalSectionGuard::CriticalSectionGuard() {
    irq_lock();
}

CriticalSectionGuard::~CriticalSectionGuard() {
    irq_unlock();
}

IsrCriticalSectionGuard::IsrCriticalSectionGuard()
    : saved_state_(0) {
    irq_lock();
}

IsrCriticalSectionGuard::~IsrCriticalSectionGuard() {
    irq_unlock();
}

namespace this_thread {

void sleep_for(Milliseconds timeout_ms) {
    (void)osal_sleep(static_cast<int>(timeout_ms));
}

bool sleep_until(uint32_t *previous_wake_tick, Milliseconds period_ms) {
    if (!previous_wake_tick) return false;
    const uint32_t now = Kernel::tick_count();
    if (now - *previous_wake_tick < period_ms) {
        sleep_for(period_ms - (now - *previous_wake_tick));
    }
    *previous_wake_tick = Kernel::tick_count();
    return true;
}

void yield() {
}

void suspend() {
    while (1) {
        asm volatile("wfi");
    }
}

} // namespace this_thread

} // namespace osal
