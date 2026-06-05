#ifndef GD32F50X_LIBOPT_H
#define GD32F50X_LIBOPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* Boolean type definitions */
typedef enum {
    DISABLE = 0,
    ENABLE = 1
} ControlStatus;

typedef enum {
    RESET = 0,
    SET = 1
} FlagStatus;

typedef enum {
    ERROR = 0,
    SUCCESS = 1
} ErrStatus;

/* NULL pointer */
#ifndef NULL
#define NULL ((void *)0)
#endif

/* Boolean definitions */
typedef enum {
    FALSE = 0,
    TRUE = 1
} bool_t;

/* Bit mask macros */
#ifndef BIT
#define BIT(x) ((uint32_t)((uint32_t)0x01U << (x)))
#endif

/* Bits macro - set bits from high to low */
#define BITS(high, low) ((uint32_t)(0xFFFFFFFFUL << (low)) & (uint32_t)(0xFFFFFFFFUL >> (31U - (high))))

/* GET_BITS macro - extract bits from a value */
#define GET_BITS(regval, high, low) (((uint32_t)(regval) & BITS((high), (low))) >> (low))

/* Register access macros */
#define REG32(addr) (*(volatile uint32_t *)(uint32_t)(addr))
#define REG16(addr) (*(volatile uint16_t *)(uint32_t)(addr))
#define REG8(addr)  (*(volatile uint8_t *)(uint32_t)(addr))

/* Oscillator values */
#ifndef HXTAL_VALUE
#define HXTAL_VALUE    ((uint32_t)8000000U)
#endif

#ifndef IRC16M_VALUE
#define IRC16M_VALUE   ((uint32_t)16000000U)
#endif

#ifndef IRC8M_VALUE
#define IRC8M_VALUE    ((uint32_t)4000000U)
#endif

#ifndef IRC48M_VALUE
#define IRC48M_VALUE   ((uint32_t)48000000U)
#endif

#ifndef LXTAL_VALUE
#define LXTAL_VALUE    ((uint32_t)32768U)
#endif

/* Timeout values */
#ifndef HXTAL_STARTUP_TIMEOUT
#define HXTAL_STARTUP_TIMEOUT   ((uint32_t)0x000FFFFFU)
#endif

#ifndef IRC8M_STARTUP_TIMEOUT
#define IRC8M_STARTUP_TIMEOUT   ((uint32_t)0x000FFFFFU)
#endif

#ifdef __cplusplus
}
#endif

#endif /* GD32F50X_LIBOPT_H */
