#include <osal.h>
#include <arch/arm/cortex_m/fault.h>

namespace osal {

EventGroup::~EventGroup() { destroy(); }

bool EventGroup::create()
{
    if (Kernel::in_isr()) return false;
    CriticalSectionGuard guard;
    if (!events_.idle()) return false;
    events_ = {};
    native_handle_ = this;
    return true;
}

void EventGroup::destroy()
{
    if (Kernel::in_isr()) {
        hal::fault::panic(hal::fault::FatalReason::KernelAssert, 0,
                          "EventGroup destroy in ISR", 0U);
    }
    CriticalSectionGuard guard;
    if (!events_.idle()) {
        hal::fault::panic(hal::fault::FatalReason::KernelAssert, 0,
                          "EventGroup has active waiters", 0U);
    }
    native_handle_ = nullptr;
    events_ = {};
}

void EventGroup::notify_waiters()
{
    events_.notify([](void* notification) {
        (void)static_cast<Semaphore*>(notification)->release();
    });
}

uint32_t EventGroup::set_bits(uint32_t bits)
{
    if (Kernel::in_isr() || !detail::EventBits<kMaxWaiters>::valid(bits)) return 0U;
    CriticalSectionGuard guard;
    if (!is_valid()) return 0U;
    const uint32_t result = events_.set(bits);
    notify_waiters();
    return result;
}

uint32_t EventGroup::clear_bits(uint32_t bits)
{
    if (Kernel::in_isr() || !detail::EventBits<kMaxWaiters>::valid(bits)) return 0U;
    CriticalSectionGuard guard;
    return is_valid() ? events_.clear(bits) : 0U;
}

uint32_t EventGroup::get_bits() const
{
    if (Kernel::in_isr()) return 0U;
    CriticalSectionGuard guard;
    return is_valid() ? events_.get() : 0U;
}

uint32_t EventGroup::wait_impl(uint32_t set, uint32_t mask, bool clear,
                               WaitMode mode, Milliseconds timeout, bool barrier)
{
    if (Kernel::in_isr() || mask == 0U
        || !detail::EventBits<kMaxWaiters>::valid(mask | set)) return 0U;
    Semaphore notification(0U, 1U);
    if (!notification.is_valid()) return 0U;
    uint32_t result = 0U;
    int ticket;
    {
        CriticalSectionGuard guard;
        if (!is_valid()) return 0U;
        ticket = events_.begin(mask, mode == WaitMode::All, clear,
                               &notification, result, set, barrier);
        notify_waiters();
        if (ticket < 0) return result;
    }
    (void)notification.take(timeout);
    CriticalSectionGuard guard;
    return events_.finish(ticket);
}

uint32_t EventGroup::wait_bits(uint32_t mask, bool clear, WaitMode mode,
                               Milliseconds timeout)
{
    return wait_impl(0U, mask, clear, mode, timeout, false);
}

uint32_t EventGroup::sync(uint32_t set, uint32_t wait, Milliseconds timeout)
{
    return wait_impl(set, wait, true, WaitMode::All, timeout, true);
}

} // namespace osal
