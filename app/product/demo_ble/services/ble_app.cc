/**
 * BLE application layer: services, advertising, event handling.
 *
 * This file contains all BLE-specific logic. main.cc only calls
 * init_ble() and the accessor functions.
 */

#include "services/ble_app.h"

#include "board/board_devices.h"
#include "comm/link_bridge.h"

#include <boot/boot_ctrl.h>
#include <boot/product_info.h>
#include <boot_layout.h>
#include <arch/arm/cortex_m/fault.h>
#include <log.h>
#include <osal.h>

#include "ble/ble_device.h"
#if defined(CONFIG_BLE_HID)
#include "ble/ble_hid.h"
#endif
#if defined(CONFIG_BLE_UART_SERVICE)
#include "ble/ble_uart.h"
#endif
#if defined(CONFIG_BLE_BATTERY_SERVICE)
#include "ble/ble_batt.h"
#endif
#if defined(CONFIG_BLE_DEVICE_INFO)
#include "ble/ble_dis.h"
#endif

#include <atomic>
#include <cstring>
#include <type_traits>

/* ---- BLE objects ---- */
static std::atomic<ble::BleStack *> s_ble {nullptr};
#if defined(CONFIG_BLE_HID)
static ble::BleHidService s_hid;
#endif
#if defined(CONFIG_BLE_UART_SERVICE)
static ble::BleUartService s_uart;
#endif
#if defined(CONFIG_BLE_BATTERY_SERVICE)
static ble::BleBattService s_batt;
#endif
#if defined(CONFIG_BLE_DEVICE_INFO)
static ble::BleDisService s_dis;
#endif

#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_UART_SERVICE)
static constexpr size_t kMaxBleCommandData = 247U;

enum class BleCommandType : uint8_t {
    Keyboard,
    Uart,
};

struct BleCommand {
    BleCommandType type {BleCommandType::Uart};
    uint16_t length {0U};
    uint8_t data[kMaxBleCommandData] {};
};

static_assert(std::is_trivially_copyable_v<BleCommand>);

static osal::MessageQueue s_ble_commands;
alignas(std::max_align_t) static std::byte s_ble_command_storage[
    osal::MessageQueue::storage_size<
        BleCommand, CONFIG_BLE_COMMAND_QUEUE_DEPTH>()] {};
static std::atomic<uint32_t> s_accepted {0U};
static std::atomic<uint32_t> s_completed {0U};
static std::atomic<uint32_t> s_retries {0U};
static std::atomic<uint32_t> s_dropped {0U};
static std::atomic<uint32_t> s_invalid {0U};
#endif

enum class BleAppEventType : uint8_t {
    AdvertisingStarted,
    Connected,
    Disconnected,
    PairSuccess,
    PairFailed,
#if defined(CONFIG_BLE_UART_SERVICE)
    UartRx,
#endif
};

struct BleAppEvent {
    BleAppEventType type {BleAppEventType::AdvertisingStarted};
    uint8_t status {0U};
    uint8_t disconnect_reason {0U};
    ble::BdAddr peer_addr {};
#if defined(CONFIG_BLE_UART_SERVICE)
    static constexpr size_t kMaxUartData = 247U;
    uint16_t length {0U};
    uint8_t data[kMaxUartData] {};
#endif
};

static_assert(std::is_trivially_copyable_v<BleAppEvent>);
static_assert(std::atomic<uint32_t>::is_always_lock_free,
              "BLE ISR ring indices must be lock-free");
static_assert(std::atomic<int32_t>::is_always_lock_free,
              "BLE ISR fatal status must be lock-free");

// One extra slot distinguishes full from empty while preserving the configured
// usable event depth.
static constexpr uint32_t kBleEventSlotCount =
    CONFIG_BLE_EVENT_QUEUE_DEPTH + 1U;
static BleAppEvent s_ble_events[kBleEventSlotCount] {};
static std::atomic<uint32_t> s_ble_event_head {0U};
static std::atomic<uint32_t> s_ble_event_tail {0U};
static std::atomic<uint32_t> s_ble_event_high_water {0U};
static std::atomic<uint32_t> s_ble_event_dropped {0U};
static std::atomic<int32_t> s_ble_fatal_error {0};
static osal::Semaphore s_ble_work {0U, 1U};
static std::atomic<bool> s_image_confirmed {false};

__attribute__((section(".product_info"), used))
const boot::ProductInfo kProductInfo = boot::make_product_info(
    boot::layout::kProductId, 0x0100, {1, 0, 0, 0});

/* ---- Link layer RX bridge ---- */
static std::atomic<app::BleRxCallback> s_link_rx_cb {nullptr};

static void signal_ble_owner()
{
    if (osal::Kernel::in_isr()) {
        osal::IsrContext context;
        (void)s_ble_work.release_from_isr(context);
    } else {
        // This is a binary work signal. An existing token already covers all
        // entries currently visible in the rings/queues.
        (void)s_ble_work.release();
    }
}

static void record_ble_fatal(int32_t error)
{
    if (error == 0) {
        error = -1;
    }
    int32_t expected = 0;
    (void)s_ble_fatal_error.compare_exchange_strong(
        expected, error, std::memory_order_release,
        std::memory_order_relaxed);
    signal_ble_owner();
}

static bool enqueue_ble_event(const BleAppEvent &event)
{
    const uint32_t head =
        s_ble_event_head.load(std::memory_order_relaxed);
    const uint32_t next = (head + 1U) % kBleEventSlotCount;
    const uint32_t tail =
        s_ble_event_tail.load(std::memory_order_acquire);
    if (next == tail) {
        s_ble_event_dropped.fetch_add(1U, std::memory_order_relaxed);
#if defined(CONFIG_BLE_UART_SERVICE)
        const bool critical = event.type != BleAppEventType::UartRx
            && event.type != BleAppEventType::PairSuccess
            && event.type != BleAppEventType::PairFailed;
#else
        const bool critical = event.type != BleAppEventType::PairSuccess
            && event.type != BleAppEventType::PairFailed;
#endif
        if (critical) {
            // Losing a connection/advertising transition could leave the Link
            // layer in a stale state. Fail closed instead of continuing with
            // an unknowable application lifecycle.
            record_ble_fatal(-2);
        }
        return false;
    }

    s_ble_events[head] = event;
    s_ble_event_head.store(next, std::memory_order_release);

    const uint32_t depth = next >= tail
        ? next - tail
        : kBleEventSlotCount - (tail - next);
    uint32_t high =
        s_ble_event_high_water.load(std::memory_order_relaxed);
    while (depth > high
           && !s_ble_event_high_water.compare_exchange_weak(
               high, depth, std::memory_order_relaxed)) {
    }
    signal_ble_owner();
    return true;
}

static bool dequeue_ble_event(BleAppEvent &event)
{
    const uint32_t tail =
        s_ble_event_tail.load(std::memory_order_relaxed);
    if (tail == s_ble_event_head.load(std::memory_order_acquire)) {
        return false;
    }
    event = s_ble_events[tail];
    s_ble_event_tail.store(
        (tail + 1U) % kBleEventSlotCount, std::memory_order_release);
    return true;
}

#if defined(CONFIG_BLE_UART_SERVICE)
static void ble_uart_rx_bridge(const uint8_t *data, size_t len, void *) {
    if (data == nullptr || len == 0U || len > BleAppEvent::kMaxUartData
        || len > UINT16_MAX) {
        s_ble_event_dropped.fetch_add(1U, std::memory_order_relaxed);
        return;
    }
    BleAppEvent event {};
    event.type = BleAppEventType::UartRx;
    event.length = static_cast<uint16_t>(len);
    std::memcpy(event.data, data, len);
    (void)enqueue_ble_event(event);
}
#endif

/* ---- HID Report Map (Keyboard + Consumer Control) ---- */
#if defined(CONFIG_BLE_HID)
static const uint8_t s_hid_report_map[] = {
    /* Keyboard */
    0x05, 0x01,       /* Usage Page (Generic Desktop) */
    0x09, 0x06,       /* Usage (Keyboard) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x01,       /* Report Id 1 */
    0x05, 0x07,       /* Usage Page (Key Codes) */
    0x19, 0xe0,       /* Usage Minimum (224) */
    0x29, 0xe7,       /* Usage Maximum (231) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x08,       /* Report Count (8) */
    0x81, 0x02,       /* Input (Data, Variable, Absolute) */
    0x95, 0x01,       /* Report Count (1) */
    0x75, 0x08,       /* Report Size (8) */
    0x81, 0x01,       /* Input (Constant) */
    0x95, 0x06,       /* Report Count (6) */
    0x75, 0x08,       /* Report Size (8) */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x65,       /* Logical Maximum (101) */
    0x05, 0x07,       /* Usage Page (Key Codes) */
    0x19, 0x00,       /* Usage Minimum (0) */
    0x29, 0x65,       /* Usage Maximum (101) */
    0x81, 0x00,       /* Input (Data, Array) */
    0xC0,             /* End Collection */

    /* Consumer Control */
    0x05, 0x0C,       /* Usage Page (Consumer) */
    0x09, 0x01,       /* Usage (Consumer Control) */
    0xA1, 0x01,       /* Collection (Application) */
    0x85, 0x02,       /* Report Id 2 */
    0x15, 0x00,       /* Logical Minimum (0) */
    0x25, 0x01,       /* Logical Maximum (1) */
    0x75, 0x01,       /* Report Size (1) */
    0x95, 0x01,       /* Report Count (1) */
    0x09, 0xE9,       /* Usage (Volume Up) */
    0x81, 0x06,
    0x09, 0xEA,       /* Usage (Volume Down) */
    0x81, 0x06,
    0x09, 0xCD,       /* Usage (Play/Pause) */
    0x81, 0x06,
    0xC0
};
#endif

/* ---- Advertising data ---- */
#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_BATTERY_SERVICE) \
    || defined(CONFIG_BLE_DEVICE_INFO)
static constexpr uint8_t kAdvertisedServiceCount =
#if defined(CONFIG_BLE_HID)
    1U +
#endif
#if defined(CONFIG_BLE_BATTERY_SERVICE)
    1U +
#endif
#if defined(CONFIG_BLE_DEVICE_INFO)
    1U +
#endif
    0U;
#endif

static const uint8_t s_adv_data[] = {
    0x02,
    0x01, /* LE General Discoverable + BR/EDR not supported */
    0x06,

    0x0C,
    0x09, /* Complete Local Name */
    'G', 'R', '5', '5', '2', '5', '_', 'D', 'e', 'm', 'o',

#if defined(CONFIG_BLE_HID)
    0x03,
    0x19, /* Appearance */
    0xC1, 0x03, /* HID Keyboard */
#endif

#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_BATTERY_SERVICE) \
    || defined(CONFIG_BLE_DEVICE_INFO)
    static_cast<uint8_t>(1U + (2U * kAdvertisedServiceCount)),
    0x03, /* Complete List of 16-bit Service UUIDs */
#if defined(CONFIG_BLE_HID)
    0x12, 0x18, /* HID Service */
#endif
#if defined(CONFIG_BLE_BATTERY_SERVICE)
    0x0F, 0x18, /* Battery Service */
#endif
#if defined(CONFIG_BLE_DEVICE_INFO)
    0x0A, 0x18, /* Device Information Service */
#endif
#endif
};

/* ---- PnP ID ---- */
#if defined(CONFIG_BLE_DEVICE_INFO)
static const ble::PnpId s_pnp_id = {
    .vendor_id_source = 1,
    .vendor_id        = 0x04F7,
    .product_id       = 0x5525,
    .product_version  = 0x0100
};
#endif

static bool init_services()
{
#if defined(CONFIG_BLE_DEVICE_INFO)
    if (s_dis.init(s_pnp_id) != ble::Status::Ok) {
        return false;
    }
#endif
#if defined(CONFIG_BLE_BATTERY_SERVICE)
    if (s_batt.init(100U) != ble::Status::Ok) {
        return false;
    }
#endif
#if defined(CONFIG_BLE_HID)
    if (s_hid.init_keyboard(
            {s_hid_report_map, sizeof(s_hid_report_map)})
        != ble::Status::Ok) {
        return false;
    }
#endif
#if defined(CONFIG_BLE_UART_SERVICE)
    if (s_uart.init(ble_uart_rx_bridge, nullptr) != ble::Status::Ok) {
        return false;
    }
#endif
    return true;
}

/* ---- BLE event callback ---- */
static void on_ble_event(const ble::Event &evt, void *) {
    BleAppEvent deferred {};
    deferred.status = evt.status;
    switch (evt.id) {
    case ble::EventId::StackInit:
        if (evt.status != 0U) {
            record_ble_fatal(evt.status);
            return;
        }
        if (!init_services()) {
            record_ble_fatal(static_cast<int32_t>(ble::Status::Error));
            return;
        }
        if (auto *stack = s_ble.load(std::memory_order_acquire);
            stack == nullptr || stack->adv_start() != ble::Status::Ok) {
            record_ble_fatal(static_cast<int32_t>(ble::Status::Error));
            return;
        }
        break;

    case ble::EventId::AdvStarted:
        deferred.type = BleAppEventType::AdvertisingStarted;
        (void)enqueue_ble_event(deferred);
        break;

    case ble::EventId::AdvStopped:
        if (evt.status != 0U) {
            record_ble_fatal(evt.status);
        }
        break;

    case ble::EventId::Connected:
        deferred.type = BleAppEventType::Connected;
        deferred.peer_addr = evt.peer_addr;
        (void)enqueue_ble_event(deferred);
        break;

    case ble::EventId::Disconnected:
        deferred.type = BleAppEventType::Disconnected;
        deferred.disconnect_reason = evt.disconnect_reason;
        (void)enqueue_ble_event(deferred);
        if (auto *stack = s_ble.load(std::memory_order_acquire);
            stack == nullptr || stack->adv_start() != ble::Status::Ok) {
            record_ble_fatal(static_cast<int32_t>(ble::Status::Error));
        }
        break;

    case ble::EventId::PairSuccess:
        deferred.type = BleAppEventType::PairSuccess;
        (void)enqueue_ble_event(deferred);
        break;

    case ble::EventId::PairFailed:
        deferred.type = BleAppEventType::PairFailed;
        (void)enqueue_ble_event(deferred);
        break;

    default:
        break;
    }
}

/* ---- Public API ---- */

namespace app {

namespace {

#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_UART_SERVICE)
bool enqueue_command(BleCommandType type, const uint8_t *data, size_t len)
{
    if (!s_ble_commands.is_valid() || data == nullptr || len == 0U
        || len > kMaxBleCommandData || len > UINT16_MAX) {
        s_invalid.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    if (!ble_is_connected()) {
        s_dropped.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    BleCommand command {};
    command.type = type;
    command.length = static_cast<uint16_t>(len);
    std::memcpy(command.data, data, len);
    if (!s_ble_commands.send(&command, 0U)) {
        return false;
    }
    s_accepted.fetch_add(1U, std::memory_order_relaxed);
    signal_ble_owner();
    return true;
}
#endif

#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_UART_SERVICE)
ble::Status dispatch_command(const BleCommand &command)
{
    auto *stack = s_ble.load(std::memory_order_acquire);
    if (stack == nullptr || !stack->is_connected()) {
        return ble::Status::NotConnected;
    }
    switch (command.type) {
    case BleCommandType::Keyboard:
#if defined(CONFIG_BLE_HID)
        return s_hid.send_keyboard_report(
            stack->conn_index(), 1U, command.data, command.length);
#else
        return ble::Status::InvalidParam;
#endif
    case BleCommandType::Uart:
#if defined(CONFIG_BLE_UART_SERVICE)
        return s_uart.send(
            stack->conn_index(), command.data, command.length);
#else
        return ble::Status::InvalidParam;
#endif
    default:
        return ble::Status::InvalidParam;
    }
}
#endif

void dispatch_app_event(const BleAppEvent &event)
{
    switch (event.type) {
    case BleAppEventType::AdvertisingStarted:
        if (!s_image_confirmed.load(std::memory_order_acquire)) {
            if (!boot::confirm_image()) {
                hal::fault::panic(hal::fault::FatalReason::InitFailure,
                                  -1, "boot_confirm", 0U);
            }
            s_image_confirmed.store(true, std::memory_order_release);
        }
        LOGI("ble", "Advertising started");
        break;
    case BleAppEventType::Connected:
        app::set_comm_connected(true);
        LOGI("ble", "Connected to %02X:%02X:%02X:%02X:%02X:%02X",
             event.peer_addr.addr[5], event.peer_addr.addr[4],
             event.peer_addr.addr[3], event.peer_addr.addr[2],
             event.peer_addr.addr[1], event.peer_addr.addr[0]);
        break;
    case BleAppEventType::Disconnected:
        app::set_comm_connected(false);
        LOGI("ble", "Disconnected (0x%02X)", event.disconnect_reason);
        break;
    case BleAppEventType::PairSuccess:
        LOGI("ble", "Pairing succeeded");
        break;
    case BleAppEventType::PairFailed:
        LOGW("ble", "Pairing failed (0x%02X)", event.status);
        break;
#if defined(CONFIG_BLE_UART_SERVICE)
    case BleAppEventType::UartRx:
        if (const app::BleRxCallback callback =
                s_link_rx_cb.load(std::memory_order_acquire);
            callback != nullptr) {
            callback(event.data, event.length);
        }
        break;
#endif
    default:
        break;
    }
}

} // namespace

int init_ble() {
    if (s_ble.load(std::memory_order_acquire) != nullptr
        || !s_ble_work.is_valid()) {
        return -1;
    }
    s_ble_event_head.store(0U, std::memory_order_relaxed);
    s_ble_event_tail.store(0U, std::memory_order_relaxed);
    s_ble_event_high_water.store(0U, std::memory_order_relaxed);
    s_ble_event_dropped.store(0U, std::memory_order_relaxed);
    s_ble_fatal_error.store(0, std::memory_order_relaxed);
    s_image_confirmed.store(false, std::memory_order_relaxed);
#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_UART_SERVICE)
    if (s_ble_commands.is_valid()
        || !s_ble_commands.create(
            CONFIG_BLE_COMMAND_QUEUE_DEPTH, sizeof(BleCommand),
            s_ble_command_storage, sizeof(s_ble_command_storage))) {
        return -1;
    }
    s_accepted.store(0U, std::memory_order_relaxed);
    s_completed.store(0U, std::memory_order_relaxed);
    s_retries.store(0U, std::memory_order_relaxed);
    s_dropped.store(0U, std::memory_order_relaxed);
    s_invalid.store(0U, std::memory_order_relaxed);
#endif

    // BLE config comes from initcall; app supplies the event callback.
    auto &ble_dev = board::ble();
    s_ble.store(&ble_dev.stack(), std::memory_order_release);
    // Advertising data must be installed before ble_stack_init() can publish
    // the asynchronous StackInit event.
    if (ble_dev.stack().set_adv_data(s_adv_data, sizeof(s_adv_data))
        != ble::Status::Ok) {
        s_ble.store(nullptr, std::memory_order_release);
#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_UART_SERVICE)
        s_ble_commands.destroy();
#endif
        return -1;
    }
    const int result = ble_dev.init(on_ble_event, nullptr);
    if (result != static_cast<int>(ble::Status::Ok)) {
        s_ble.store(nullptr, std::memory_order_release);
#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_UART_SERVICE)
        s_ble_commands.destroy();
#endif
        return result != 0 ? result : -1;
    }
    return 0;
}

bool ble_is_connected() {
    auto *stack = s_ble.load(std::memory_order_acquire);
    return stack != nullptr && stack->is_connected();
}
bool ble_send_keyboard(const uint8_t *report, size_t len) {
#if defined(CONFIG_BLE_HID)
    if (len != 8U) {
        s_invalid.fetch_add(1U, std::memory_order_relaxed);
        return false;
    }
    return enqueue_command(BleCommandType::Keyboard, report, len);
#else
    (void)report;
    (void)len;
    return false;
#endif
}

bool ble_send_uart(const uint8_t *data, size_t len) {
#if defined(CONFIG_BLE_UART_SERVICE)
    return enqueue_command(BleCommandType::Uart, data, len);
#else
    (void)data;
    (void)len;
    return false;
#endif
}

bool process_ble_work(uint32_t wait_ms) {
    if (s_ble_work.take(wait_ms) != 0) {
        return false;
    }

    const int32_t fatal_error =
        s_ble_fatal_error.load(std::memory_order_acquire);
    if (fatal_error != 0) {
        hal::fault::panic(hal::fault::FatalReason::InitFailure,
                          fatal_error, "ble_startup", 0U);
    }

    bool processed = false;
    BleAppEvent event {};
    while (dequeue_ble_event(event)) {
        dispatch_app_event(event);
        processed = true;
    }

#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_UART_SERVICE)
    BleCommand command {};
    if (!s_ble_commands.receive(&command, 0U)) {
        return processed;
    }

    osal::Deadline deadline(CONFIG_BLE_COMMAND_TIMEOUT_MS);
    ble::Status status = ble::Status::Busy;
    do {
        status = dispatch_command(command);
        if (status != ble::Status::Busy) {
            break;
        }
        s_retries.fetch_add(1U, std::memory_order_relaxed);
        osal::this_thread::sleep_for(1U);
    } while (!deadline.expired());

    if (status == ble::Status::Ok) {
        s_completed.fetch_add(1U, std::memory_order_relaxed);
    } else {
        s_dropped.fetch_add(1U, std::memory_order_relaxed);
    }
    // A binary work signal may have coalesced several producer notifications.
    // Keep the owner runnable until every queued command has been considered.
    if (s_ble_commands.count() > 0U) {
        (void)s_ble_work.release();
    }
    processed = true;
#endif
    return processed;
}

BleRuntimeStats ble_runtime_stats() {
#if defined(CONFIG_BLE_HID) || defined(CONFIG_BLE_UART_SERVICE)
    const osal::MessageQueueStats queue = s_ble_commands.stats();
    return {
        queue.capacity,
        queue.current_depth,
        queue.high_water_mark,
        queue.send_failures,
        s_ble_event_high_water.load(std::memory_order_relaxed),
        s_ble_event_dropped.load(std::memory_order_relaxed),
        s_accepted.load(std::memory_order_relaxed),
        s_completed.load(std::memory_order_relaxed),
        s_retries.load(std::memory_order_relaxed),
        s_dropped.load(std::memory_order_relaxed),
        s_invalid.load(std::memory_order_relaxed),
    };
#else
    return {
        0U,
        0U,
        0U,
        0U,
        s_ble_event_high_water.load(std::memory_order_relaxed),
        s_ble_event_dropped.load(std::memory_order_relaxed),
    };
#endif
}

void set_ble_rx_callback(BleRxCallback cb) {
    s_link_rx_cb.store(cb, std::memory_order_release);
}

} // namespace app
