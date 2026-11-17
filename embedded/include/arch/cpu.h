#ifndef ZEPHYR_INCLUDE_ARCH_CPU_H_
#define ZEPHYR_INCLUDE_ARCH_CPU_H_

#ifndef _ASMLANGUAGE
#include <toolchain.h>
#include <stddef.h>
#include <stdint.h>
#endif

#if defined(CONFIG_ARM)
#include <arch/arm/arch.h>
#endif

#endif
