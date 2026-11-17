#pragma once
#include <cstdint>

namespace system_safety {
using Stop = void (*)(void*);
[[nodiscard]] int register_output(void* context, Stop stop);
void unregister_output(int id);
void trip(uint32_t reason);
[[nodiscard]] bool latched();
}
