#pragma once

#include <toolchain/gcc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*initcall_fn_t)(void);

struct initcall_entry {
    initcall_fn_t fn;
};

#define _INITCALL_SECTION(level) .initcall.level
#define _INITCALL_VAR2(counter) _initcall_entry_##counter
#define _INITCALL_VAR(counter) _INITCALL_VAR2(counter)
#define DECLARE_INITCALL(fn, level)                                        \
    static const Z_DECL_ALIGN(struct initcall_entry)                       \
        Z_GENERIC_SECTION(_INITCALL_SECTION(level)) __used                 \
        _INITCALL_VAR(__COUNTER__) = { reinterpret_cast<initcall_fn_t>(fn) }

void run_initcalls(void);

#ifdef __cplusplus
}
#endif
