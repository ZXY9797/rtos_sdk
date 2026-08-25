#include <device.h>
#include <device_pm.h>
#include <osal.h>

#include <cstdio>
#include <cstdlib>

#define CHECK(condition)                                                   \
    do {                                                                   \
        if (!(condition)) {                                                \
            std::fprintf(stderr, "CHECK failed at %s:%d: %s\n",          \
                         __FILE__, __LINE__, #condition);                  \
            std::abort();                                                  \
        }                                                                  \
    } while (false)

namespace {

struct FakeDevice {
    int ordinal;
    int suspend_error {0};
    int resume_error {0};
};

FakeDevice devices[] = {{1}, {2}, {3}};
int transitions[8] {};
size_t transition_count = 0U;

int suspend_device(void* instance)
{
    auto* device = static_cast<FakeDevice*>(instance);
    transitions[transition_count++] = -device->ordinal;
    return device->suspend_error;
}

int resume_device(void* instance)
{
    auto* device = static_cast<FakeDevice*>(instance);
    transitions[transition_count++] = device->ordinal;
    return device->resume_error;
}

bool ready(const void*)
{
    return true;
}

void reset_transitions()
{
    transition_count = 0U;
    for (int& transition : transitions) {
        transition = 0;
    }
}

} // namespace

namespace osal {

bool Kernel::in_isr()
{
    return false;
}

} // namespace osal

namespace hal {

const DeviceInfo* get_device_registry(size_t* count)
{
    static const DeviceInfo registry[] = {
        {1, "one", "Fake", &devices[0], ready, suspend_device,
         resume_device, 10U, true},
        {2, "two", "Fake", &devices[1], ready, suspend_device,
         resume_device, 20U, true},
        {3, "three", "Fake", &devices[2], ready, suspend_device,
         resume_device, 30U, true},
    };
    if (count != nullptr) {
        *count = sizeof(registry) / sizeof(registry[0]);
    }
    return registry;
}

} // namespace hal

int main()
{
    CHECK(hal::suspend_devices());
    CHECK(transition_count == 3U);
    CHECK(transitions[0] == -3 && transitions[1] == -2
          && transitions[2] == -1);
    CHECK(hal::device_power_status().state
          == hal::DevicePowerState::Suspended);

    reset_transitions();
    CHECK(hal::resume_devices());
    CHECK(transition_count == 3U);
    CHECK(transitions[0] == 1 && transitions[1] == 2
          && transitions[2] == 3);

    reset_transitions();
    devices[1].suspend_error = -20;
    CHECK(!hal::suspend_devices());
    CHECK(transition_count == 3U);
    CHECK(transitions[0] == -3 && transitions[1] == -2
          && transitions[2] == 3);
    CHECK(hal::device_power_status().state
          == hal::DevicePowerState::Active);

    reset_transitions();
    devices[1].suspend_error = 0;
    devices[0].suspend_error = -10;
    devices[2].resume_error = -30;
    CHECK(!hal::suspend_devices());
    CHECK(transition_count == 5U);
    CHECK(transitions[0] == -3 && transitions[1] == -2
          && transitions[2] == -1 && transitions[3] == 2
          && transitions[4] == 3);
    const hal::DevicePowerStatus status = hal::device_power_status();
    CHECK(status.state == hal::DevicePowerState::Error);
    CHECK(status.failed_ordinal == 3);
    CHECK(status.error == -30);
    CHECK(status.transitioned_devices == 1U);

    std::puts("device PM tests passed");
    return 0;
}
