#include <arch/arm/cortex_m/fault.h>
#include <cmsis_core.h>
#include <log.h>
#include <init.h>
#include <cstdarg>
#include <cstdio>

namespace hal::fault {

// ============================================================
//  底层输出（异常上下文专用，不依赖 RTOS/LOG）
//  putc 是弱符号，SoC 层可覆盖为 UART 寄存器直写
// ============================================================

[[gnu::weak]] void putc(char c) { (void)c; }
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
//  格式化记录输出（共享工具）
// ============================================================

static constexpr size_t RECORD_BUF_SIZE = 1024;

static size_t formatRecord(const FaultRecord &rec, char *buf, size_t bufSize) {
    size_t pos = 0;

    auto append = [&](const char *fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int n = vsnprintf(buf + pos, bufSize - pos, fmt, ap);
        va_end(ap);
        if (n > 0) pos += static_cast<size_t>(n);
    };

    auto appendCfsr = [&](uint32_t cfsr) {
        if (cfsr & Cfsr::MMFSR_MASK) {
            append("  MemManage:");
            if (cfsr & Cfsr::IACCVIOL)  append(" IACCVIOL");
            if (cfsr & Cfsr::DACCVIOL)  append(" DACCVIOL");
            if (cfsr & Cfsr::MUNSTKERR) append(" MUNSTKERR");
            if (cfsr & Cfsr::MSTKERR)   append(" MSTKERR");
            if (cfsr & Cfsr::MLSPERR)   append(" MLSPERR");
            if (cfsr & Cfsr::MMARVALID) { append(" MMFAR="); append("0x%08X", SCB->MMFAR); }
            append("\n");
        }
        if (cfsr & Cfsr::BFSR_MASK) {
            append("  BusFault:");
            if (cfsr & Cfsr::IBUSERR)     append(" IBUSERR");
            if (cfsr & Cfsr::PRECISERR)   append(" PRECISERR");
            if (cfsr & Cfsr::IMPRECISERR) append(" IMPRECISERR");
            if (cfsr & Cfsr::UNSTKERR)    append(" UNSTKERR");
            if (cfsr & Cfsr::STKERR)      append(" STKERR");
            if (cfsr & Cfsr::LSPERR)      append(" LSPERR");
            if (cfsr & Cfsr::BFARVALID)   { append(" BFAR="); append("0x%08X", SCB->BFAR); }
            append("\n");
        }
        if (cfsr & Cfsr::UFSR_MASK) {
            append("  UsageFault:");
            if (cfsr & Cfsr::UNDEFINSTR) append(" UNDEFINSTR");
            if (cfsr & Cfsr::INVSTATE)   append(" INVSTATE");
            if (cfsr & Cfsr::INVPC)      append(" INVPC");
            if (cfsr & Cfsr::NOCP)       append(" NOCP");
            if (cfsr & Cfsr::UNALIGNED)  append(" UNALIGNED");
            if (cfsr & Cfsr::DIVBYZERO)  append(" DIVBYZERO");
            append("\n");
        }
    };

    auto appendHfsr = [&](uint32_t hfsr) {
        if (hfsr & Hfsr::VECTTBL)  append("  HFSR: VECTTBL\n");
        if (hfsr & Hfsr::FORCED)   append("  HFSR: FORCED\n");
        if (hfsr & Hfsr::DEBUGEVT) append("  HFSR: DEBUGEVT\n");
    };

    append("R0 : 0x%08X\n", rec.frame.r0);
    append("R1 : 0x%08X\n", rec.frame.r1);
    append("R2 : 0x%08X\n", rec.frame.r2);
    append("R3 : 0x%08X\n", rec.frame.r3);
    append("R12: 0x%08X\n", rec.frame.r12);
    append("LR : 0x%08X\n", rec.frame.lr);
    append("PC : 0x%08X\n", rec.frame.pc);
    append("xPSR: 0x%08X\n", rec.frame.xpsr);
    append("EXC_RETURN: 0x%08X\n", rec.excReturn);
    append("CFSR: 0x%08X\n", rec.cfsr);
    append("HFSR: 0x%08X\n", rec.hfsr);
    appendCfsr(rec.cfsr);
    appendHfsr(rec.hfsr);
    append("MSP: 0x%08X\n", rec.msp);
    append("PSP: 0x%08X\n", rec.psp);

    if (rec.backtraceDepth > 0) {
        append("Backtrace (%d frames):\n", rec.backtraceDepth);
        for (int i = 0; i < rec.backtraceDepth; ++i) {
            append("  #%d 0x%08X\n", i, rec.backtrace[i]);
        }
    }

    if (rec.snapshotSp) {
        append("Stack @ 0x%08X:\n", rec.snapshotSp);
        for (int i = 0; i < FaultRecord::STACK_SNAPSHOT_WORDS; i += 4) {
            append("0x%08X: ", rec.snapshotSp + i * 4);
            for (int j = 0; j < 4 && (i + j) < FaultRecord::STACK_SNAPSHOT_WORDS; ++j) {
                append("0x%08X ", rec.stackSnapshot[i + j]);
            }
            append("\n");
        }
    }

    return pos;
}

// 异常上下文：逐字符输出到 putc
static void printRecord(const FaultRecord &rec) {
    char buf[RECORD_BUF_SIZE];
    size_t len = formatRecord(rec, buf, sizeof(buf));
    for (size_t i = 0; i < len; ++i) {
        putc(buf[i]);
    }
}

// ============================================================
//  noinit 故障记录
// ============================================================

#ifdef CONFIG_FAULT_NOINIT_BACKEND
__attribute__((section(".noinit")))
static FaultRecord s_faultRecord;

const FaultRecord *getRecord() { return &s_faultRecord; }

class NoinitBackend : public IBackend {
public:
    void onFault(const FaultRecord &rec) override {
        s_faultRecord = rec;
    }
    void onBoot() override {}
    void clear() override {
        s_faultRecord.magic = 0;
    }
};

static NoinitBackend s_noinitBackend;
#else
const FaultRecord *getRecord() { return nullptr; }
#endif

// ============================================================
//  UartBackend
//  - onFault: 异常上下文，使用 putc 弱符号（不依赖 RTOS）
//  - onBoot:  正常线程上下文，使用 LOG 模块
// ============================================================

#ifdef CONFIG_FAULT_UART_BACKEND
class UartBackend : public IBackend {
public:
    void onFault(const FaultRecord &rec) override {
        print("===== FAULT =====\n");
        printRecord(rec);
        print("==================\n");
    }
    void onBoot() override {
#ifdef CONFIG_FAULT_NOINIT_BACKEND
        if (!s_faultRecord.valid()) return;
        char buf[RECORD_BUF_SIZE];
        formatRecord(s_faultRecord, buf, sizeof(buf));
        log_write(LogLevel::Error, "fault", "===== LAST FAULT (noinit) =====\n%s"
                   "================================", buf);
#endif
    }
};

static UartBackend s_uartBackend;
#endif

// ============================================================
//  后端注册
// ============================================================

static IBackend *s_backends[MAX_BACKENDS] = {};
static int s_backendCount = 0;

void registerBackend(IBackend *backend) {
    if (s_backendCount < MAX_BACKENDS) {
        s_backends[s_backendCount++] = backend;
    }
}

void notifyFault(const FaultRecord &rec) {
    for (int i = 0; i < s_backendCount; ++i) {
        s_backends[i]->onFault(rec);
    }
}

// SYS_INIT 自动初始化：注册后端 + 触发 onBoot
static int fault_init() {
#ifdef CONFIG_FAULT_NOINIT_BACKEND
    registerBackend(&s_noinitBackend);
#endif
#ifdef CONFIG_FAULT_UART_BACKEND
    registerBackend(&s_uartBackend);
#endif

    for (int i = 0; i < s_backendCount; ++i) {
        s_backends[i]->onBoot();
    }
    return 0;
}

SYS_INIT(fault_init, INITCALL_LEVEL_PRE_KERNEL_1, 10);

void dump() {
#ifdef CONFIG_FAULT_NOINIT_BACKEND
    if (!s_faultRecord.valid()) return;
    char buf[RECORD_BUF_SIZE];
    formatRecord(s_faultRecord, buf, sizeof(buf));
    log_write(LogLevel::Error, "fault", "===== LAST FAULT (noinit) =====\n%s"
               "================================", buf);
#endif
}

void clear() {
    for (int i = 0; i < s_backendCount; ++i) {
        s_backends[i]->clear();
    }
}

// ============================================================
//  帧指针回溯
// ============================================================

static int unwindFramePointer(uint32_t fp, uint32_t *out, int maxDepth) {
    int depth = 0;
    for (int i = 0; i < maxDepth; ++i) {
        if (fp == 0 || (fp & 0x3)) break;
        uint32_t *frame = reinterpret_cast<uint32_t *>(fp);
        uint32_t nextFp = frame[0];
        uint32_t retAddr = frame[1];
        if (retAddr == 0) break;
        out[depth++] = retAddr;
        if (nextFp <= fp) break;
        fp = nextFp;
    }
    return depth;
}

static void captureStackSnapshot(uint32_t sp, FaultRecord &rec) {
    rec.snapshotSp = sp;
    uint32_t *src = reinterpret_cast<uint32_t *>(sp);
    for (int i = 0; i < FaultRecord::STACK_SNAPSHOT_WORDS; ++i) {
        rec.stackSnapshot[i] = src[i];
    }
}

// ============================================================
//  FaultRecord 构建
// ============================================================

static FaultRecord buildRecord(const Frame *frame, uint32_t excReturn) {
    const bool usedPsp = (excReturn & 0x04) != 0;
    const uint32_t msp = __get_MSP();
    const uint32_t psp = __get_PSP();
    const uint32_t activeSp = usedPsp ? psp : msp;

    FaultRecord rec{};
    rec.magic      = FaultRecord::MAGIC;
    rec.cfsr       = SCB->CFSR;
    rec.hfsr       = SCB->HFSR;
    rec.mmfar      = SCB->MMFAR;
    rec.bfar       = SCB->BFAR;
    rec.excReturn  = excReturn;
    rec.frame      = *frame;
    rec.msp        = msp;
    rec.psp        = psp;

#ifdef CONFIG_FAULT_BACKTRACE
    // 帧指针回溯
    uint32_t fpReg = usedPsp ? frame->r7 : frame->r11;
    rec.backtrace[0] = frame->lr;
    rec.backtraceDepth = 1 +
        unwindFramePointer(fpReg, &rec.backtrace[1],
                           FaultRecord::MAX_BACKTRACE - 1);
#endif

#ifdef CONFIG_FAULT_STACK_SNAPSHOT
    // 栈快照
    captureStackSnapshot(activeSp, rec);
#endif

    return rec;
}

} // namespace hal::fault

// ============================================================
//  extern "C" 入口
// ============================================================

extern "C" [[noreturn]]
void arm_fault_handler(const hal::fault::Frame *frame, uint32_t excReturn)
{
    using namespace hal::fault;

    // 1. 构建完整记录
    FaultRecord rec = buildRecord(frame, excReturn);

    // 2. 遍历所有后端
    notifyFault(rec);

    // 3. 死循环
    for (;;) {}
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
    for (;;) {}
}
