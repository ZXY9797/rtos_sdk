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
constexpr uintptr_t kSysTickControl = 0xE000E010U;
constexpr uintptr_t kSysTickLoad = 0xE000E014U;
constexpr uintptr_t kSysTickValue = 0xE000E018U;
constexpr uint32_t kSysTickEnable = 1U << 0U;
constexpr uint32_t kSysTickInterrupt = 1U << 1U;
constexpr uint32_t kSysTickCoreClock = 1U << 2U;
constexpr uint32_t kSysTickMaxCycles = 0x01000000U;

osal_bare_sem g_sems[kMaxSemaphores];
osal_bare_mutex g_mutexes[kMaxMutexes];
volatile uint32_t g_ticks;
uintptr_t g_scheduler_irq_state;
uint32_t g_scheduler_suspend_depth;

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

uintptr_t irq_lock() {
    uintptr_t state;
    asm volatile("mrs %0, primask" : "=r"(state) :: "memory");
    asm volatile("cpsid i" ::: "memory");
    return state;
}

void irq_restore(uintptr_t state) {
    asm volatile("msr primask, %0" :: "r"(state) : "memory");
}

bool wait_expired(uint32_t start, uint32_t timeout_ms) {
    if (timeout_ms == OSAL_WAITING_FOREVER) return false;
    if (timeout_ms == 0U) return true;
    return (osal::Kernel::uptime_ms() - start) >= timeout_ms;
}

} // namespace

int osal_sleep(int ms) {
    if (ms <= 0) return 0;
    const uint64_t requested_ticks =
        (static_cast<uint64_t>(static_cast<uint32_t>(ms))
         * CONFIG_SYS_CLOCK_TICKS_PER_SEC + 999U) / 1000U;
    const uint32_t wait_ticks = requested_ticks > UINT32_MAX
        ? UINT32_MAX : static_cast<uint32_t>(requested_ticks);
    const uint32_t start = g_ticks;
    while ((g_ticks - start) < wait_ticks) {
        asm volatile("wfi");
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

void osal_yield_from_isr(int reschedule) {
    (void)reschedule;
}

void sys_clock_announce(uint32_t ticks) {
    g_ticks += ticks;
}

int osal_init(void) {
    if (CONFIG_SYS_CLOCK_TICKS_PER_SEC == 0U
        || (CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC
            % CONFIG_SYS_CLOCK_TICKS_PER_SEC) != 0U) {
        return -1;
    }
    constexpr uint32_t cycles_per_tick =
        CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC
        / CONFIG_SYS_CLOCK_TICKS_PER_SEC;
    if (cycles_per_tick == 0U || cycles_per_tick > kSysTickMaxCycles) {
        return -1;
    }
    *reinterpret_cast<volatile uint32_t *>(kSysTickLoad) =
        cycles_per_tick - 1U;
    *reinterpret_cast<volatile uint32_t *>(kSysTickValue) = 0U;
    *reinterpret_cast<volatile uint32_t *>(kSysTickControl) =
        kSysTickEnable | kSysTickInterrupt | kSysTickCoreClock;
    return 0;
}

extern "C" void SysTick_Handler(void) {
    sys_clock_announce(1U);
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
    uintptr_t ipsr;
    asm volatile("mrs %0, ipsr" : "=r"(ipsr));
    return ipsr != 0U;
}

uint32_t Kernel::tick_count() {
    return g_ticks;
}

uint32_t Kernel::uptime_ms() {
    const uint64_t ticks = tick_count();
    return static_cast<uint32_t>(
        (ticks * 1000U) / CONFIG_SYS_CLOCK_TICKS_PER_SEC);
}

void Kernel::suspend_scheduler() {
    if (g_scheduler_suspend_depth == UINT32_MAX) {
        return;
    }
    if (g_scheduler_suspend_depth == 0U) {
        g_scheduler_irq_state = irq_lock();
    }
    g_scheduler_suspend_depth = g_scheduler_suspend_depth + 1U;
}

bool Kernel::resume_scheduler() {
    if (g_scheduler_suspend_depth == 0U) return false;
    g_scheduler_suspend_depth = g_scheduler_suspend_depth - 1U;
    if (g_scheduler_suspend_depth == 0U) {
        irq_restore(g_scheduler_irq_state);
    }
    return true;
}

Semaphore::Semaphore(uint32_t initial, uint32_t max_count) {
    if (max_count == 0U || initial > max_count) return;
    const uintptr_t state = irq_lock();
    handle_ = alloc_slot(g_sems);
    irq_restore(state);
    if (!handle_) return;
    handle_->count = initial;
    handle_->max_count = max_count;
    max_count_ = max_count;
}

Semaphore::~Semaphore() {
    const uintptr_t state = irq_lock();
    free_slot(handle_);
    irq_restore(state);
    handle_ = nullptr;
    max_count_ = 0;
}

int Semaphore::take(Milliseconds timeout_ms) {
    if (!handle_) return -1;
    const uint32_t start = Kernel::uptime_ms();
    while (true) {
        const uintptr_t state = irq_lock();
        if (handle_->count > 0U) {
            handle_->count = handle_->count - 1U;
            irq_restore(state);
            return 0;
        }
        irq_restore(state);

        if (wait_expired(start, timeout_ms)) return -1;
    }
}

int Semaphore::release() {
    if (!handle_) return -1;
    const uintptr_t state = irq_lock();
    if (handle_->count < handle_->max_count) {
        handle_->count = handle_->count + 1U;
        irq_restore(state);
        return 0;
    }
    irq_restore(state);
    return -1;
}

int Semaphore::release_from_isr(IsrContext& context) {
    (void)context;
    return release();
}

uint32_t Semaphore::count() const {
    const uintptr_t state = irq_lock();
    const uint32_t count = handle_ ? handle_->count : 0U;
    irq_restore(state);
    return count;
}

Mutex::Mutex() {
    (void)create();
}

Mutex::~Mutex() {
    destroy();
}

bool Mutex::create() {
    if (handle_) return true;
    const uintptr_t state = irq_lock();
    handle_ = alloc_slot(g_mutexes);
    irq_restore(state);
    return handle_ != nullptr;
}

void Mutex::destroy() {
    const uintptr_t state = irq_lock();
    free_slot(handle_);
    irq_restore(state);
    handle_ = nullptr;
}

int Mutex::lock(Milliseconds timeout_ms) {
    if (!handle_) return -1;
    const uint32_t start = Kernel::uptime_ms();
    while (true) {
        const uintptr_t state = irq_lock();
        if (!handle_->locked) {
            handle_->locked = true;
            irq_restore(state);
            return 0;
        }
        irq_restore(state);
        if (wait_expired(start, timeout_ms)) return -1;
    }
}

int Mutex::try_lock() {
    return lock(0);
}

int Mutex::unlock() {
    if (!handle_) return -1;
    const uintptr_t state = irq_lock();
    handle_->locked = false;
    irq_restore(state);
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

int IsrTrigger::register_slot(Callback cb, void *arg) {
    if (!cb) return -1;
    const uintptr_t state = irq_lock();
    for (int id = 0; id < kMaxSlots; ++id) {
        if (s_slots[id].cb == nullptr) {
            s_slots[id] = {cb, arg};
            irq_restore(state);
            return id;
        }
    }
    irq_restore(state);
    return -1;
}

void IsrTrigger::unregister_slot(int id) {
    if (id < 0 || id >= kMaxSlots) return;
    const uintptr_t state = irq_lock();
    s_slots[id] = {};
    irq_restore(state);
}

void IsrTrigger::fire(int id) {
    if (id >= 0 && id < kMaxSlots && s_slots[id].cb) {
        s_slots[id].cb(s_slots[id].arg);
    }
}

PeriodicThread::PeriodicThread(PrivateTag) {
}

PeriodicThread::~PeriodicThread() {
    IsrTrigger::unregister_slot(trigger_id_);
    trigger_id_ = -1;
}

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
                                       PeriodicTrigger, IrqTimer *, bool) {
    return nullptr;
}

int PeriodicThread::startup() { return -1; }
int PeriodicThread::stop() { return 0; }
int PeriodicThread::notify_from_isr(uint32_t) { return -1; }

EventGroup::~EventGroup() {
    destroy();
}

bool EventGroup::create() {
    native_handle_ = nullptr;
    return false;
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

bool MessageQueue::create(uint32_t, size_t, void *, size_t) {
    item_size_ = 0U;
    native_handle_ = nullptr;
    return false;
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
    if (!create(buf, buf_size, 1U)) {
        rtos_free(buf);
        return false;
    }
    owns_storage_ = true;
    return true;
}

bool StreamBuffer::create(uint8_t *storage, size_t storage_size, size_t) {
    destroy();
    if (!storage || storage_size == 0) return false;

    const uintptr_t irq_state = irq_lock();
    auto *state = alloc_slot(g_streams);
    irq_restore(irq_state);
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
    uint8_t *storage = state != nullptr ? state->buf : nullptr;
    const bool release_storage = owns_storage_;
    const uintptr_t irq_state = irq_lock();
    free_slot(state);
    irq_restore(irq_state);
    handle_ = nullptr;
    control_block_ = nullptr;
    owns_storage_ = false;
    if (release_storage) rtos_free(storage);
}

size_t StreamBuffer::send(const uint8_t *data, size_t len, Milliseconds) {
    IsrContext context;
    return send_from_isr(data, len, context);
}

size_t StreamBuffer::send_from_isr(const uint8_t *data, size_t len,
                                   IsrContext& context) {
    auto *state = static_cast<StreamState *>(control_block_);
    if (!state || !data || len == 0) return 0;

    size_t written = 0;
    const uintptr_t irq_state = irq_lock();
    while (written < len && state->count < state->size) {
        state->buf[state->head] = data[written++];
        state->head = (state->head + 1U) % state->size;
        state->count = state->count + 1U;
    }
    irq_restore(irq_state);

    (void)context;
    return written;
}

size_t StreamBuffer::receive(uint8_t *data, size_t len,
                             Milliseconds timeout_ms) {
    auto *state = static_cast<StreamState *>(control_block_);
    if (!state || !data || len == 0) return 0;

    const uint32_t start = Kernel::uptime_ms();
    while (state->count == 0U) {
        if (wait_expired(start, timeout_ms)) return 0;
    }

    IsrContext context;
    return receive_from_isr(data, len, context);
}

size_t StreamBuffer::receive_from_isr(uint8_t *data, size_t len,
                                      IsrContext& context) {
    auto *state = static_cast<StreamState *>(control_block_);
    if (!state || !data || len == 0) return 0;

    size_t read = 0;
    const uintptr_t irq_state = irq_lock();
    while (read < len && state->count > 0U) {
        data[read++] = state->buf[state->tail];
        state->tail = (state->tail + 1U) % state->size;
        state->count = state->count - 1U;
    }
    irq_restore(irq_state);

    (void)context;
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
    const uintptr_t irq_state = irq_lock();
    state->head = 0;
    state->tail = 0;
    state->count = 0;
    irq_restore(irq_state);
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
    (void)callback;
    (void)context;
    callback_ = nullptr;
    context_ = nullptr;
    native_handle_ = nullptr;
    return false;
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

CriticalSectionGuard::CriticalSectionGuard()
    : saved_state_(irq_lock()) {
}

CriticalSectionGuard::~CriticalSectionGuard() {
    irq_restore(saved_state_);
}

IsrCriticalSectionGuard::IsrCriticalSectionGuard()
    : saved_state_(irq_lock()) {
}

IsrCriticalSectionGuard::~IsrCriticalSectionGuard() {
    irq_restore(saved_state_);
}

namespace this_thread {

void sleep_for(Milliseconds timeout_ms) {
    (void)osal_sleep(static_cast<int>(timeout_ms));
}

bool sleep_until(uint32_t *previous_wake_tick, Milliseconds period_ms) {
    if (!previous_wake_tick) return false;
    const uint64_t scaled =
        static_cast<uint64_t>(period_ms) * CONFIG_SYS_CLOCK_TICKS_PER_SEC;
    const uint32_t period_ticks = static_cast<uint32_t>(
        std::max<uint64_t>(1U, (scaled + 999U) / 1000U));
    const uint32_t now = Kernel::tick_count();
    const uint32_t elapsed = now - *previous_wake_tick;
    if (elapsed < period_ticks) {
        const uint32_t remaining_ticks = period_ticks - elapsed;
        const uint32_t remaining_ms = static_cast<uint32_t>(
            (static_cast<uint64_t>(remaining_ticks) * 1000U
             + CONFIG_SYS_CLOCK_TICKS_PER_SEC - 1U)
            / CONFIG_SYS_CLOCK_TICKS_PER_SEC);
        sleep_for(remaining_ms);
    }
    *previous_wake_tick = *previous_wake_tick + period_ticks;
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
