#include <system/output_safety.h>
#include <runtime/output_latch.h>
#include <irq.h>

namespace system_safety {
namespace { runtime::OutputLatch<8U> outputs; }
int register_output(void* context, Stop stop) {
    hal::IrqGuard guard;
    return outputs.add(context, stop);
}
void unregister_output(int id) { hal::IrqGuard guard; outputs.remove(id); }
void trip(uint32_t reason) { hal::IrqGuard guard; outputs.trip(reason); }
bool latched() { hal::IrqGuard guard; return outputs.latched(); }
}
