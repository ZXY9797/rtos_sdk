#pragma once
#include <cstdint>

namespace hal::fault {

// ---- 数据结构 ----

struct Frame {
    uint32_t r0, r1, r2, r3;
    uint32_t r12, lr, pc, xpsr;
    uint32_t r4, r5, r6, r7;
    uint32_t r8, r9, r10, r11;
};

struct FaultRecord {
    static constexpr uint32_t MAGIC = 0xFA17FAUL;
    static constexpr int MAX_BACKTRACE = 16;
    static constexpr int STACK_SNAPSHOT_WORDS = 32;  // 128 bytes

    uint32_t magic;
    uint32_t cfsr;
    uint32_t hfsr;
    uint32_t mmfar;
    uint32_t bfar;
    uint32_t excReturn;
    Frame    frame;

    uint32_t msp;
    uint32_t psp;
    uint32_t backtrace[MAX_BACKTRACE];
    int      backtraceDepth;
    uint32_t stackSnapshot[STACK_SNAPSHOT_WORDS];
    uint32_t snapshotSp;

    [[nodiscard]] bool valid() const { return magic == MAGIC; }
};

struct Cfsr {
    static constexpr uint32_t MMFSR_MASK  = 0xFF;
    static constexpr uint32_t BFSR_MASK   = 0xFF00;
    static constexpr uint32_t UFSR_MASK   = 0xFFFF0000;
    static constexpr uint32_t MMFSR_SHIFT = 0;
    static constexpr uint32_t BFSR_SHIFT  = 8;
    static constexpr uint32_t UFSR_SHIFT  = 16;

    static constexpr uint32_t MMARVALID = 1u << 7;
    static constexpr uint32_t MLSPERR   = 1u << 5;
    static constexpr uint32_t MSTKERR   = 1u << 4;
    static constexpr uint32_t MUNSTKERR = 1u << 3;
    static constexpr uint32_t DACCVIOL  = 1u << 1;
    static constexpr uint32_t IACCVIOL  = 1u << 0;
    static constexpr uint32_t BFARVALID = 1u << 15;
    static constexpr uint32_t LSPERR    = 1u << 13;
    static constexpr uint32_t STKERR    = 1u << 12;
    static constexpr uint32_t UNSTKERR  = 1u << 11;
    static constexpr uint32_t IMPRECISERR = 1u << 10;
    static constexpr uint32_t PRECISERR = 1u << 9;
    static constexpr uint32_t IBUSERR   = 1u << 8;
    static constexpr uint32_t DIVBYZERO = 1u << 25;
    static constexpr uint32_t UNALIGNED = 1u << 24;
    static constexpr uint32_t NOCP      = 1u << 19;
    static constexpr uint32_t INVPC     = 1u << 18;
    static constexpr uint32_t INVSTATE  = 1u << 17;
    static constexpr uint32_t UNDEFINSTR = 1u << 16;
};

struct Hfsr {
    static constexpr uint32_t DEBUGEVT = 1u << 31;
    static constexpr uint32_t FORCED   = 1u << 30;
    static constexpr uint32_t VECTTBL   = 1u << 1;
};

// ---- 可插拔后端接口 ----

class IBackend {
public:
    virtual ~IBackend() = default;

    // fault 发生时调用（中断上下文，需快速返回）
    virtual void onFault(const FaultRecord &rec) = 0;

    // 启动时调用（检查/输出历史记录）
    virtual void onBoot() {}

    // 清除记录
    virtual void clear() {}
};

// 注册后端（最多 MAX_BACKENDS 个）
static constexpr int MAX_BACKENDS = 4;
void registerBackend(IBackend *backend);

// 供架构层调用：遍历所有后端执行 onFault
void notifyFault(const FaultRecord &rec);

// ---- 底层输出（工具函数）----

[[gnu::weak]] void putc(char c);
void print(const char *s);
void printHex(uint32_t val);
void printDec(uint32_t val);

// ---- noinit 记录快捷操作 ----

[[nodiscard]] const FaultRecord *getRecord();
void dump();     // 通过 UartBackend 打印 noinit 记录
void clear();    // 遍历所有后端执行 clear()

} // namespace hal::fault

// ---- extern "C" 入口 ----
extern "C" {
[[noreturn]] void arm_fault_handler(const hal::fault::Frame *frame, uint32_t excReturn);
}

// assert_post_action 在 __assert.h 中声明（C++ 链接）
