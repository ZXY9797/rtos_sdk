#include <devicetree.h>
#include <device.h>
#include <drivers_generated.h>
#include <osal/osal.h>

using Led    = hal::Device<DT_ORD(DT_ALIAS(led0))>;
using Button = hal::Device<DT_ORD(DT_ALIAS(sw0))>;

static Led led;
static Button button;

static void test_thread(void*)
{
    bool last_state = false;
    while (true) {
        bool state = button.is_on();
        if (state && !last_state) {
            led.toggle();
        }
        last_state = state;
        osal::this_thread::sleep_for(50);
    }
}

int main()
{
    (void)led.configure(GPIO_OUTPUT_HIGH | GPIO_PULL_UP);
    (void)button.configure(GPIO_INPUT | GPIO_PULL_DOWN);

    auto *tid = osal::Thread::create("test", test_thread, nullptr, 1024, 8, 10);
    if (tid) {
        (void)tid->startup();
    }

    while (true) {}
}
