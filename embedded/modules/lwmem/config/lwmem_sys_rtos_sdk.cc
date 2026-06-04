#include "system/lwmem_sys.h"

#if LWMEM_CFG_OS && !__DOXYGEN__

/*
 * To use this module, options must be defined as
 *
 * #define LWMEM_CFG_OS_MUTEX_HANDLE        TX_MUTEX
 */

/* Include ThreadX API module */
#include <osal.h>

static uint8_t lwmem_mutex_invalid_flag = 0;

uint8_t
lwmem_sys_mutex_create(LWMEM_CFG_OS_MUTEX_HANDLE* m) {
    static char name[] = "lwmem_mutex";
    int ret = osal_mutex_init(m, name, OSAL_IPC_FLAG_PRIO);

    if (ret == 0) {
        lwmem_mutex_invalid_flag = 1;
    } else {
        lwmem_mutex_invalid_flag = 0;
    }

    return lwmem_mutex_invalid_flag;
}

uint8_t
lwmem_sys_mutex_isvalid(LWMEM_CFG_OS_MUTEX_HANDLE* m) {
    (void)m;
    return lwmem_mutex_invalid_flag;
}

uint8_t
lwmem_sys_mutex_wait(LWMEM_CFG_OS_MUTEX_HANDLE* m) {
    return osal_mutex_take(m, OSAL_WAITING_FOREVER) == 0;
}

uint8_t
lwmem_sys_mutex_release(LWMEM_CFG_OS_MUTEX_HANDLE* m) {
    return osal_mutex_release(m) == 0;
}

#endif /* LWMEM_CFG_OS && !__DOXYGEN__ */
