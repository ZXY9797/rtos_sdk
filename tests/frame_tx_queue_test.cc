#include <link/frame_tx_queue.h>
#include <link/fragment.h>
#include <array>
#include <cstdlib>
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

int main()
{
    link::FrameTxQueue<512U, 2U> queue;
    std::array<uint8_t, 512U> frame {};
    for (size_t i = 0U; i < frame.size(); ++i) frame[i] = static_cast<uint8_t>(i);
    CHECK(queue.enqueue(frame.data(), frame.size(), 7U));
    CHECK(queue.enqueue(frame.data(), frame.size(), 7U));
    CHECK(!queue.enqueue(frame.data(), 1U, 7U));
    link::Reassembler receive;
    unsigned completions = 0U;
    while (queue.pending() > 0U) {
        unsigned mailboxes = 3U;
        queue.process([&](const uint8_t* data, size_t size) {
            if (mailboxes == 0U) return link::FragmentSendResult::Busy;
            --mailboxes;
            if (link::Fragmenter::recv(data, size, receive)) {
                std::array<uint8_t, 512U> received {};
                CHECK(receive.read(received.data(), received.size()) == 512);
                CHECK(received == frame);
                ++completions;
            }
            return link::FragmentSendResult::Sent;
        }, 8U);
    }
    CHECK(completions == 2U && queue.completed() == 2U && queue.high_water() == 2U);
    CHECK(queue.enqueue(frame.data(), frame.size(), 7U));
    queue.process([](const uint8_t*, size_t) { return link::FragmentSendResult::Failed; });
    CHECK(queue.failed() == 1U && queue.pending() == 0U);
    CHECK(queue.enqueue(frame.data(), frame.size(), 7U));
    queue.reset();
    CHECK(queue.pending() == 0U && queue.epoch() == 1U);
}
