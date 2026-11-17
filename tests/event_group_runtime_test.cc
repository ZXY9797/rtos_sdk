#include <osal.h>
#include <arch/arm/cortex_m/fault.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

struct osal_bare_sem { std::mutex mutex; std::condition_variable condition; unsigned tokens; };
namespace { std::recursive_mutex critical; std::atomic<unsigned> waiting {0U}; }

namespace osal {
bool Kernel::in_isr() { return false; }
CriticalSectionGuard::CriticalSectionGuard() { critical.lock(); }
CriticalSectionGuard::~CriticalSectionGuard() { critical.unlock(); }
Semaphore::Semaphore(uint32_t initial, uint32_t maximum) : max_count_(maximum) {
    handle_ = new osal_bare_sem {{}, {}, initial};
}
Semaphore::~Semaphore() { delete handle_; }
int Semaphore::take(Milliseconds timeout) {
    std::unique_lock<std::mutex> lock(handle_->mutex);
    waiting.fetch_add(1U);
    bool ready;
    if (timeout == kWaitForever) {
        handle_->condition.wait(lock, [&] { return handle_->tokens > 0U; }); ready = true;
    } else {
        ready = handle_->condition.wait_for(lock, std::chrono::milliseconds(timeout),
                                             [&] { return handle_->tokens > 0U; });
    }
    waiting.fetch_sub(1U);
    if (!ready) return -1;
    --handle_->tokens;
    return 0;
}
int Semaphore::release() {
    std::lock_guard<std::mutex> lock(handle_->mutex);
    if (handle_->tokens == max_count_) return -1;
    ++handle_->tokens;
    handle_->condition.notify_one();
    return 0;
}
}

namespace hal::fault {
[[noreturn]] void panic(FatalReason, int32_t, const char*, uint32_t) { std::abort(); }
}

int main()
{
    osal::EventGroup group;
    CHECK(group.create());
    auto barrier = [&](uint32_t bit) {
        for (unsigned round = 0U; round < 1000U; ++round) {
            CHECK((group.sync(bit, 3U, 3000U) & 3U) == 3U);
        }
    };
    std::thread first(barrier, 1U), second(barrier, 2U);
    first.join(); second.join();
    CHECK(group.get_bits() == 0U);
    std::thread a([&] { CHECK(group.wait_bits(1U, true, osal::EventGroup::WaitMode::Any, 3000U) == 5U); });
    std::thread b([&] { CHECK(group.wait_bits(1U, true, osal::EventGroup::WaitMode::Any, 3000U) == 5U); });
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (waiting.load() != 2U) {
        CHECK(std::chrono::steady_clock::now() < limit);
        std::this_thread::yield();
    }
    CHECK(!group.create());
    CHECK(group.set_bits(5U) == 4U);
    a.join(); b.join();
    CHECK(group.clear_bits(4U) == 4U);
    CHECK(group.set_bits(0x80000000U) == 0U && group.get_bits() == 0U);
    CHECK(group.wait_bits(1U, false, osal::EventGroup::WaitMode::Any, 1U) == 0U);
    group.destroy();
}
