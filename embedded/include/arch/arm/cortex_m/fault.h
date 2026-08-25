#pragma once

#include <cstddef>
#include <cstdint>

namespace hal::fault {

enum class FatalReason : uint32_t {
    Exception = 1U,
    InitFailure = 2U,
    OsalFailure = 3U,
    Assert = 4U,
    StackOverflow = 5U,
    AllocationFailure = 6U,
    MainReturned = 7U,
    KernelAssert = 8U,
    ThreadShutdownTimeout = 9U,
    WatchdogExpired = 10U,
};

struct Frame {
    // Must match fault.S: software-saved registers precede the copied
    // hardware exception frame in memory.
    uint32_t r4, r5, r6, r7;
    uint32_t r8, r9, r10, r11;
    uint32_t r0, r1, r2, r3;
    uint32_t r12, lr, pc, xpsr;
};

struct FaultRecord {
    // Low byte is the persistent format version. Version 3 also records
    // non-exception fatal events under the same commit-last contract.
    static constexpr uint32_t MAGIC = 0xFA17FA03U;
    static constexpr int MAX_BACKTRACE = 16;
    static constexpr int STACK_SNAPSHOT_WORDS = 32;
    static constexpr size_t CONTEXT_SIZE = 24U;

    uint32_t magic;
    FatalReason reason;
    int32_t detail;
    uint32_t line;
    uint32_t contextHash;
    char context[CONTEXT_SIZE];
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t excReturn;
    Frame frame;

    uint32_t msp;
    uint32_t psp;
    uint32_t backtrace[MAX_BACKTRACE];
    int backtraceDepth;
    uint32_t stackSnapshot[STACK_SNAPSHOT_WORDS];
    uint32_t snapshotSp;
    uint32_t crc32;

    [[nodiscard]] uint32_t calculateCrc() const {
        const auto *bytes = reinterpret_cast<const uint8_t *>(this);
        uint32_t crc = 0xFFFFFFFFU;
        for (size_t index = offsetof(FaultRecord, reason);
             index < offsetof(FaultRecord, crc32); ++index) {
            crc ^= bytes[index];
            for (uint8_t bit = 0U; bit < 8U; ++bit) {
                crc = (crc & 1U) != 0U
                    ? (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
            }
        }
        return ~crc;
    }

    [[nodiscard]] bool valid() const {
        return magic == MAGIC
            && reason >= FatalReason::Exception
            && reason <= FatalReason::WatchdogExpired
            && backtraceDepth >= 0
            && backtraceDepth <= MAX_BACKTRACE
            && crc32 == calculateCrc();
    }
};

struct Cfsr {
    static constexpr uint32_t MMFSR_MASK = 0xFFU;
    static constexpr uint32_t BFSR_MASK = 0xFF00U;
    static constexpr uint32_t UFSR_MASK = 0xFFFF0000U;

    static constexpr uint32_t MMARVALID = 1U << 7U;
    static constexpr uint32_t MLSPERR = 1U << 5U;
    static constexpr uint32_t MSTKERR = 1U << 4U;
    static constexpr uint32_t MUNSTKERR = 1U << 3U;
    static constexpr uint32_t DACCVIOL = 1U << 1U;
    static constexpr uint32_t IACCVIOL = 1U << 0U;
    static constexpr uint32_t BFARVALID = 1U << 15U;
    static constexpr uint32_t LSPERR = 1U << 13U;
    static constexpr uint32_t STKERR = 1U << 12U;
    static constexpr uint32_t UNSTKERR = 1U << 11U;
    static constexpr uint32_t IMPRECISERR = 1U << 10U;
    static constexpr uint32_t PRECISERR = 1U << 9U;
    static constexpr uint32_t IBUSERR = 1U << 8U;
    static constexpr uint32_t DIVBYZERO = 1U << 25U;
    static constexpr uint32_t UNALIGNED = 1U << 24U;
    static constexpr uint32_t NOCP = 1U << 19U;
    static constexpr uint32_t INVPC = 1U << 18U;
    static constexpr uint32_t INVSTATE = 1U << 17U;
    static constexpr uint32_t UNDEFINSTR = 1U << 16U;
};

struct Hfsr {
    static constexpr uint32_t DEBUGEVT = 1U << 31U;
    static constexpr uint32_t FORCED = 1U << 30U;
    static constexpr uint32_t VECTTBL = 1U << 1U;
};

// Fault callbacks run with interrupts disabled. They must not allocate,
// acquire locks, call the scheduler, or rely on mutable object lifetime.
using FaultCallback = void (*)(void *context, const FaultRecord &record);
using BackendCallback = void (*)(void *context);

struct Backend {
    void *context {nullptr};
    FaultCallback on_fault {nullptr};
    BackendCallback on_boot {nullptr};
    BackendCallback clear {nullptr};
};

static constexpr size_t MAX_BACKENDS = 4U;

// Accepted only before PRE_KERNEL_1 fault_init freezes the registry. The
// descriptor is copied, so its caller-side lifetime is irrelevant.
[[nodiscard]] bool registerBackend(const Backend &backend);
void notifyFault(const FaultRecord &record);

// Persist the fatal context and reset the MCU. This path is safe before the
// scheduler starts and does not allocate, lock, or use normal logging.
[[noreturn]] void panic(FatalReason reason, int32_t detail,
                        const char *context, uint32_t line);

// When CONFIG_FAULT_UART_BACKEND is enabled, the product/SoC must provide a
// strong, bounded, polling implementation. Otherwise a weak no-op is used for
// assert diagnostics without claiming that a UART backend exists.
void putc(char character);
void print(const char *text);
void printHex(uint32_t value);
void printDec(uint32_t value);

[[nodiscard]] const FaultRecord *getRecord();
void dump();
void clear();

} // namespace hal::fault

extern "C" {
[[noreturn]] void arm_fault_handler(const hal::fault::Frame *frame,
                                    uint32_t excReturn,
                                    uint32_t activeSp);
}
