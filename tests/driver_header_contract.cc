#include <device.h>
#include <device_pm.h>
#include <drivers/can.h>
#include <drivers/can_dlc.h>
#include <drivers/i2c.h>
#include <drivers/spi.h>
#include <drivers/uart.h>
#include <osal.h>
#include <sensor_core.h>
#include <system/watchdog.h>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<osal::Mutex>);
static_assert(!std::is_copy_constructible_v<osal::Semaphore>);
static_assert(osal::kMutexControlBytes > 0U);
static_assert(osal::kSemaphoreControlBytes > 0U);
static_assert(system_watchdog::kMaxClients > 0U);

int driver_header_contract()
{
    hal::CanConfig can {};
    hal::DevicePowerStatus power {};
    osal::MessageQueueStats queue {};
    return (can.bitrate > 0U && power.error == 0
            && queue.current_depth == 0U) ? 0 : 1;
}
