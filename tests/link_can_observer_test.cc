#include <link/can_link.h>
#include <array>
#include <deque>
#include <cstdio>
#include <cstdlib>

#define CHECK(value) do { \
    if (!(value)) { \
        std::fprintf(stderr, "CHECK: %s:%d\n", __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (false)

struct osal_bare_mutex {};
namespace osal {
Mutex::Mutex() : handle_(new osal_bare_mutex {}) {}
Mutex::~Mutex() { delete handle_; }
int Mutex::lock(Milliseconds) { return 0; }
int Mutex::unlock() { return 0; }
}

struct CanFrame { uint32_t id; std::array<uint8_t, 8> data; uint8_t size; bool extended; };
static std::deque<CanFrame> received;
static unsigned receive_count = 0U;
namespace hal {
Can& Can::instance(uint8_t port) { static Can first(0U), second(1U); return port ? second : first; }
bool Can::is_started() const { return true; }
Status Can::init(const CanConfig&) { m_initialized.store(true); return Status::Ok; }
Status Can::send(uint32_t, const uint8_t*, uint8_t, uint32_t) { return Status::Ok; }
Status Can::get_rx_message(uint32_t* id, uint8_t* data, uint8_t capacity, uint8_t* size, bool* extended)
{
    if (received.empty()) return Status::Busy;
    const auto next = received.front(); received.pop_front();
    CHECK(next.size <= capacity);
    *id = next.id; *size = next.size; *extended = next.extended;
    std::memcpy(data, next.data.data(), next.size);
    ++receive_count;
    return Status::Ok;
}
}

int main()
{
    unsigned observed = 0U;
    CHECK(hal::Can::instance(0U).init({}) == hal::Status::Ok);
    link::CanLink transport(0U, 0x100U, 7U,
        [](void* context, uint32_t id, const uint8_t* bytes, size_t size, bool extended) {
            CHECK(id == 0x680U && size == 3U && bytes[0] == 1U && !extended);
            ++*static_cast<unsigned*>(context);
        }, &observed);
    received.push_back({0x100U, {0U, 11U, 22U}, 3U, false});
    received.push_back({0x680U, {1U, 2U, 3U}, 3U, false});
    received.push_back({0x100U, {0x81U, 33U, 44U}, 3U, false});
    transport.poll();
    CHECK(observed == 1U && receive_count == 3U && received.empty());
    uint8_t data[8] {};
    CHECK(transport.recv(data, sizeof(data)) == 4);
    CHECK(data[0] == 11U && data[3] == 44U);
    CHECK(transport.recv(data, sizeof(data)) == 0);
}
