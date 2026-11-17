#include <osal.h>
#include <drivers_generated.h>
#include <boot/product_info.h>
#include <boot_layout.h>
#include <arch/arm/cortex_m/fault.h>
#include <cstddef>

namespace {
__attribute__((section(".product_info"), used))
const boot::ProductInfo information = boot::make_product_info(boot::layout::kProductId, 1U, {1U, 0U, 0U, 0U});
osal::EventGroup barrier;
osal::Thread workers[2];
alignas(std::max_align_t) uint8_t stacks[2][1024];
uint32_t bits[2] {1U, 2U};
std::atomic<unsigned> completed {0U};
void check(bool condition) {
    if (!condition) hal::fault::panic(hal::fault::FatalReason::Assert, 0, "OSAL contract", 0U);
}
void worker(void* argument) {
    const uint32_t bit = *static_cast<uint32_t*>(argument);
    for (unsigned round = 0U; round < 1000U; ++round) check((barrier.sync(bit, 3U, 1000U) & 3U) == 3U);
    completed.fetch_add(1U);
}
}

int main()
{
    check(barrier.create());
    for (unsigned i = 0U; i < 2U; ++i) {
        osal::ThreadConfig config {"contract", osal::kDefaultThreadPriority, sizeof(stacks[i]), stacks[i]};
        check(workers[i].start(worker, &bits[i], config) && workers[i].startup() == 0);
    }
    osal::Deadline deadline(10000U);
    while (completed.load() != 2U && !deadline.expired()) osal::this_thread::sleep_for(1U);
    check(completed.load() == 2U);
    for (auto& worker_thread : workers) check(worker_thread.shutdown(1000U));
    check(barrier.set_bits(5U) == 5U && barrier.clear_bits(1U) == 5U && barrier.get_bits() == 4U);
    const char marker[] = "OSAL_CONTRACT_PASS\r\n";
    size_t sent = 0U;
    check(device_get(uart0).send(reinterpret_cast<const uint8_t*>(marker),
        sizeof(marker) - 1U, &sent, 1000U) == hal::Status::Ok);
    for (;;) osal::this_thread::sleep_for(1000U);
}
