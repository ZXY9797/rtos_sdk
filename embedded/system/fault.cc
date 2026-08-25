#include <arch/arm/cortex_m/fault.h>
#include <cmsis_core.h>
#include <init.h>
#include <cstdarg>

extern "C" {
extern uint8_t __ram_region_start;
extern uint8_t __ram_region_end;
}

namespace hal::fault {

// ============================================================
//  Fault-context output: no RTOS or normal logging dependencies.
//  Enabling CONFIG_FAULT_UART_BACKEND requires a strong product provider.
// ============================================================

#if !defined(CONFIG_FAULT_UART_BACKEND)
[[gnu::weak]] void putc(char c) { (void)c; }
#endif
void print(const char *s) { while (*s) putc(*s++); }

void printHex(uint32_t val) {
    print("0x");
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        putc(nibble < 10 ? '0' + nibble : 'A' + nibble - 10);
    }
}

void printDec(uint32_t val) {
    char buf[10];
    int i = 0;
    if (val == 0) { putc('0'); return; }
    while (val) { buf[i++] = '0' + val % 10; val /= 10; }
    while (i--) putc(buf[i]);
}

static void vprint(const char *fmt, va_list ap) {
    while (*fmt) {
        if (*fmt != '%') { putc(*fmt++); continue; }
        ++fmt;
        switch (*fmt) {
        case 's': print(va_arg(ap, const char *)); break;
        case 'd': printDec(va_arg(ap, uint32_t)); break;
        case 'x': printHex(va_arg(ap, uint32_t)); break;
        case 'u': printDec(va_arg(ap, uint32_t)); break;
        case '%': putc('%'); break;
        default:  putc('%'); putc(*fmt); break;
        }
        ++fmt;
    }
}

// ============================================================
//  逐字段记录输出（无 libc/heap/锁）
// ============================================================


// Fault context output must not use libc formatting, heap allocation, locks,
// or a large stack buffer. Emit each field through the polling putc hook.
static void printNamedHex(const char *name, uint32_t value) {
    print(name);
    printHex(value);
    putc('\n');
}

static void printRecord(const FaultRecord &rec) {
    printNamedHex("R0 : ", rec.frame.r0);
    printNamedHex("R1 : ", rec.frame.r1);
    printNamedHex("R2 : ", rec.frame.r2);
    printNamedHex("R3 : ", rec.frame.r3);
    printNamedHex("R12: ", rec.frame.r12);
    printNamedHex("LR : ", rec.frame.lr);
    printNamedHex("PC : ", rec.frame.pc);
    printNamedHex("xPSR: ", rec.frame.xpsr);
    printNamedHex("EXC_RETURN: ", rec.excReturn);
    printNamedHex("CFSR: ", rec.cfsr);
    printNamedHex("HFSR: ", rec.hfsr);

    if ((rec.cfsr & Cfsr::MMFSR_MASK) != 0U) {
        print("  MemManage:");
        if ((rec.cfsr & Cfsr::IACCVIOL) != 0U)  print(" IACCVIOL");
        if ((rec.cfsr & Cfsr::DACCVIOL) != 0U)  print(" DACCVIOL");
        if ((rec.cfsr & Cfsr::MUNSTKERR) != 0U) print(" MUNSTKERR");
        if ((rec.cfsr & Cfsr::MSTKERR) != 0U)   print(" MSTKERR");
        if ((rec.cfsr & Cfsr::MLSPERR) != 0U)   print(" MLSPERR");
        if ((rec.cfsr & Cfsr::MMARVALID) != 0U) {
            print(" MMFAR=");
            printHex(rec.mmfar);
        }
        putc('\n');
    }
    if ((rec.cfsr & Cfsr::BFSR_MASK) != 0U) {
        print("  BusFault:");
        if ((rec.cfsr & Cfsr::IBUSERR) != 0U)     print(" IBUSERR");
        if ((rec.cfsr & Cfsr::PRECISERR) != 0U)   print(" PRECISERR");
        if ((rec.cfsr & Cfsr::IMPRECISERR) != 0U) print(" IMPRECISERR");
        if ((rec.cfsr & Cfsr::UNSTKERR) != 0U)    print(" UNSTKERR");
        if ((rec.cfsr & Cfsr::STKERR) != 0U)      print(" STKERR");
        if ((rec.cfsr & Cfsr::LSPERR) != 0U)      print(" LSPERR");
        if ((rec.cfsr & Cfsr::BFARVALID) != 0U) {
            print(" BFAR=");
            printHex(rec.bfar);
        }
        putc('\n');
    }
    if ((rec.cfsr & Cfsr::UFSR_MASK) != 0U) {
        print("  UsageFault:");
        if ((rec.cfsr & Cfsr::UNDEFINSTR) != 0U) print(" UNDEFINSTR");
        if ((rec.cfsr & Cfsr::INVSTATE) != 0U)   print(" INVSTATE");
        if ((rec.cfsr & Cfsr::INVPC) != 0U)      print(" INVPC");
        if ((rec.cfsr & Cfsr::NOCP) != 0U)       print(" NOCP");
        if ((rec.cfsr & Cfsr::UNALIGNED) != 0U)  print(" UNALIGNED");
        if ((rec.cfsr & Cfsr::DIVBYZERO) != 0U)  print(" DIVBYZERO");
        putc('\n');
    }
    if ((rec.hfsr & Hfsr::VECTTBL) != 0U)  print("  HFSR: VECTTBL\n");
    if ((rec.hfsr & Hfsr::FORCED) != 0U)   print("  HFSR: FORCED\n");
    if ((rec.hfsr & Hfsr::DEBUGEVT) != 0U) print("  HFSR: DEBUGEVT\n");

    printNamedHex("MSP: ", rec.msp);
    printNamedHex("PSP: ", rec.psp);

    if (rec.backtraceDepth > 0) {
        print("Backtrace (");
        printDec(static_cast<uint32_t>(rec.backtraceDepth));
        print(" frames):\n");
        for (int i = 0; i < rec.backtraceDepth; ++i) {
            print("  #");
            printDec(static_cast<uint32_t>(i));
            putc(' ');
            printHex(rec.backtrace[i]);
            putc('\n');
        }
    }

    if (rec.snapshotSp != 0U) {
        print("Stack @ ");
        printHex(rec.snapshotSp);
        print(":\n");
        for (int i = 0; i < FaultRecord::STACK_SNAPSHOT_WORDS; i += 4) {
            printHex(rec.snapshotSp + static_cast<uint32_t>(i * 4));
            print(": ");
            for (int j = 0;
                 j < 4 && (i + j) < FaultRecord::STACK_SNAPSHOT_WORDS; ++j) {
                printHex(rec.stackSnapshot[i + j]);
                putc(' ');
            }
            putc('\n');
        }
    }
}

// ============================================================
//  noinit 故障记录
// ============================================================

#ifdef CONFIG_FAULT_NOINIT_BACKEND
__attribute__((section(".noinit")))
static FaultRecord s_faultRecord;

const FaultRecord *getRecord() { return &s_faultRecord; }

static void noinit_on_fault(void *, const FaultRecord &rec) {
    s_faultRecord.magic = 0U;
    __DSB();
    const auto *source = reinterpret_cast<const unsigned char *>(&rec);
    auto *destination = reinterpret_cast<volatile unsigned char *>(
        &s_faultRecord);
    for (size_t index = sizeof(s_faultRecord.magic);
         index < sizeof(FaultRecord); ++index) {
        destination[index] = source[index];
    }
    __DSB();
    s_faultRecord.magic = FaultRecord::MAGIC;
    __DSB();
}

static void noinit_clear(void *) {
    s_faultRecord.magic = 0U;
}
#else
const FaultRecord *getRecord() { return nullptr; }
#endif

// ============================================================
//  UART backend. Both paths use the required polling putc provider.
// ============================================================

#ifdef CONFIG_FAULT_UART_BACKEND
static void uart_on_fault(void *, const FaultRecord &rec) {
    print("===== FAULT =====\n");
    printRecord(rec);
    print("==================\n");
}

static void uart_on_boot(void *) {
#ifdef CONFIG_FAULT_NOINIT_BACKEND
    if (!s_faultRecord.valid()) return;
    print("===== LAST FAULT (noinit) =====\n");
    printRecord(s_faultRecord);
    print("================================\n");
#endif
}
#endif

// ============================================================
//  后端注册
// ============================================================

static Backend s_backends[MAX_BACKENDS] {};
static size_t s_backendCount = 0U;
static bool s_registryFrozen = false;

static bool backend_equal(const Backend &lhs, const Backend &rhs) {
    return lhs.context == rhs.context
        && lhs.on_fault == rhs.on_fault
        && lhs.on_boot == rhs.on_boot
        && lhs.clear == rhs.clear;
}

bool registerBackend(const Backend &backend) {
    if (backend.on_fault == nullptr) return false;

    const uint32_t previous_mask = __get_PRIMASK();
    __disable_irq();
    bool accepted = !s_registryFrozen;
    for (size_t index = 0U; accepted && index < s_backendCount; ++index) {
        if (backend_equal(s_backends[index], backend)) {
            accepted = true;
            if (previous_mask == 0U) __enable_irq();
            return accepted;
        }
    }
    if (accepted && s_backendCount < MAX_BACKENDS) {
        s_backends[s_backendCount++] = backend;
    } else {
        accepted = false;
    }
    if (previous_mask == 0U) __enable_irq();
    return accepted;
}

void notifyFault(const FaultRecord &rec) {
#ifdef CONFIG_FAULT_NOINIT_BACKEND
    // Built-in backends must work before PRE_KERNEL_1 fault_init() runs.
    noinit_on_fault(nullptr, rec);
#endif
#ifdef CONFIG_FAULT_UART_BACKEND
    uart_on_fault(nullptr, rec);
#endif
    for (size_t index = 0U; index < s_backendCount; ++index) {
        s_backends[index].on_fault(s_backends[index].context, rec);
    }
}

// SYS_INIT 自动初始化：启动期输出持久化记录。Built-in fault handling
// itself does not depend on this initcall, so early faults are still captured.
static int fault_init() {
    const uint32_t previous_mask = __get_PRIMASK();
    __disable_irq();
    s_registryFrozen = true;
    if (previous_mask == 0U) __enable_irq();
#ifdef CONFIG_FAULT_NOINIT_BACKEND
    // Noinit capture requires no boot-time action.
#endif
#ifdef CONFIG_FAULT_UART_BACKEND
    uart_on_boot(nullptr);
#endif

    for (size_t index = 0U; index < s_backendCount; ++index) {
        if (s_backends[index].on_boot != nullptr) {
            s_backends[index].on_boot(s_backends[index].context);
        }
    }
    return 0;
}

static int fault_deinit() {
    const uint32_t previous_mask = __get_PRIMASK();
    __disable_irq();
    for (size_t index = 0U; index < s_backendCount; ++index) {
        s_backends[index] = {};
    }
    s_backendCount = 0U;
    s_registryFrozen = false;
    if (previous_mask == 0U) __enable_irq();
    return 0;
}

SYS_INIT_ROLLBACK(fault_init, fault_deinit,
                  INITCALL_LEVEL_PRE_KERNEL_1, 10);

void dump() {
#ifdef CONFIG_FAULT_NOINIT_BACKEND
    if (!s_faultRecord.valid()) return;
    print("===== LAST FAULT (noinit) =====\n");
    printRecord(s_faultRecord);
    print("================================\n");
#endif
}

void clear() {
#ifdef CONFIG_FAULT_NOINIT_BACKEND
    noinit_clear(nullptr);
#endif
    for (size_t index = 0U; index < s_backendCount; ++index) {
        if (s_backends[index].clear != nullptr) {
            s_backends[index].clear(s_backends[index].context);
        }
    }
}

// ============================================================
//  帧指针回溯
// ============================================================

static bool ramRangeValid(uint32_t address, size_t length) {
    const uintptr_t ramStart = reinterpret_cast<uintptr_t>(&__ram_region_start);
    const uintptr_t ramEnd = reinterpret_cast<uintptr_t>(&__ram_region_end);
    const uint64_t end = static_cast<uint64_t>(address) + length;
    return ramStart < ramEnd && address >= ramStart && end <= ramEnd;
}

static int unwindFramePointer(uint32_t fp, uint32_t *out, int maxDepth) {
    int depth = 0;
    for (int i = 0; i < maxDepth; ++i) {
        if ((fp & 0x3U) != 0U || !ramRangeValid(fp, 2U * sizeof(uint32_t))) {
            break;
        }
        const auto *frame = reinterpret_cast<const uint32_t *>(fp);
        const uint32_t nextFp = frame[0];
        const uint32_t retAddr = frame[1];
        if ((retAddr & 0x1U) == 0U) break;
        out[depth++] = retAddr;
        if (nextFp <= fp || !ramRangeValid(nextFp, 2U * sizeof(uint32_t))) {
            break;
        }
        fp = nextFp;
    }
    return depth;
}

static void captureStackSnapshot(uint32_t sp, FaultRecord &rec) {
    constexpr size_t snapshotBytes = sizeof(rec.stackSnapshot);
    if ((sp & 0x3U) != 0U || !ramRangeValid(sp, snapshotBytes)) {
        rec.snapshotSp = 0U;
        return;
    }
    rec.snapshotSp = sp;
    const auto *src = reinterpret_cast<const uint32_t *>(sp);
    for (int i = 0; i < FaultRecord::STACK_SNAPSHOT_WORDS; ++i) {
        rec.stackSnapshot[i] = src[i];
    }
}

// ============================================================
//  FaultRecord 构建
// ============================================================

static FaultRecord s_activeFaultRecord;

static void buildRecord(FaultRecord &rec, const Frame *frame,
                        uint32_t excReturn, uint32_t activeSp) {
    const bool usedPsp = (excReturn & 0x04) != 0;
    const uint32_t msp = __get_MSP();
    const uint32_t psp = __get_PSP();

    rec = {};
    rec.magic      = FaultRecord::MAGIC;
    rec.cfsr       = SCB->CFSR;
    rec.hfsr       = SCB->HFSR;
    rec.mmfar      = SCB->MMFAR;
    rec.bfar       = SCB->BFAR;
    rec.excReturn  = excReturn;
    if (frame != nullptr
        && ramRangeValid(static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(frame)), sizeof(Frame))) {
        rec.frame = *frame;
    }
    rec.msp        = usedPsp ? msp : activeSp;
    rec.psp        = usedPsp ? activeSp : psp;

#ifdef CONFIG_FAULT_BACKTRACE
    // 帧指针回溯
    if (frame != nullptr
        && ramRangeValid(static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(frame)), sizeof(Frame))) {
        const uint32_t fpReg = usedPsp ? rec.frame.r7 : rec.frame.r11;
        rec.backtrace[0] = rec.frame.lr;
        rec.backtraceDepth = 1 +
            unwindFramePointer(fpReg, &rec.backtrace[1],
                               FaultRecord::MAX_BACKTRACE - 1);
    }
#endif

#ifdef CONFIG_FAULT_STACK_SNAPSHOT
    // 栈快照
    captureStackSnapshot(activeSp, rec);
#endif

    rec.crc32 = rec.calculateCrc();
}

} // namespace hal::fault

// ============================================================
//  extern "C" 入口
// ============================================================

extern "C" [[noreturn]]
void arm_fault_handler(const hal::fault::Frame *frame, uint32_t excReturn,
                       uint32_t activeSp)
{
    using namespace hal::fault;

    __disable_irq();

    // 1. 构建完整记录
    buildRecord(s_activeFaultRecord, frame, excReturn, activeSp);

    // 2. 遍历所有后端
    notifyFault(s_activeFaultRecord);

    // 3. Stop all normal execution. WFI keeps the failed system quiescent
    // while still allowing a debugger to inspect the captured record.
    __DSB();
    for (;;) {
        __WFI();
    }
}

void assert_print(const char *fmt, ...) {
    using namespace hal::fault;
    va_list ap;
    va_start(ap, fmt);
    vprint(fmt, ap);
    va_end(ap);
}

[[gnu::weak]] void
assert_post_action(const char *file, unsigned int line) {
    using namespace hal::fault;
    print("ASSERT FAIL @ ");
    print(file);
    print(":");
    printDec(line);
    putc('\n');
    __disable_irq();
    __DSB();
    for (;;) {
        __WFI();
    }
}
