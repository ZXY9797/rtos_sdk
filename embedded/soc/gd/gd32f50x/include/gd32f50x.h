#ifndef GD32F50X_H
#define GD32F50X_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* Include GD32F503 specific definitions (registers, base addresses) */
#include "gd32f503.h"

/* System clock frequency */
extern uint32_t SystemCoreClock;

/* System initialization function */
extern void SystemInit(void);

#ifdef __cplusplus
}
#endif

#endif /* GD32F50X_H */
