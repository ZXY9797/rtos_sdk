#ifndef LWMEM_OPTS_HDR_H
#define LWMEM_OPTS_HDR_H

#include <osal.h>

/**
 * \brief           Enables `1` or disables `0` operating system support in the library
 *
 * \note            When `LWMEM_CFG_OS` is enabled, user must implement functions in \ref LWMEM_SYS group.
 */
#define LWMEM_CFG_OS 1


/**
 * \brief           Mutex handle type
 *
 * \note            This value must be set in case \ref LWMEM_CFG_OS is set to `1`.
 *                  If data type is not known to compiler, include header file with
 *                  definition before you define handle type
 */
#define LWMEM_CFG_OS_MUTEX_HANDLE osal_mutex_t


/**
 * \brief           Number of bits to align memory address and memory size
 *
 * Some CPUs do not offer unaligned memory access (Cortex-M0 as an example)
 * therefore it is important to have alignment of data addresses and potentialy length of data
 *
 * \note            This value must be a power of `2` for number of bytes.
 *                  Usually alignment of `4` bytes fits to all processors.
 */
#define LWMEM_CFG_ALIGN_NUM 4


/**
 * \brief           Enables `1` or disables `0` full memory management support.
 *
 * When enabled (default config), library supports allocation, reallocation and freeing of the memory.
 *  - Memory [c]allocation
 *  - Memory reallocation
 *  - Memory allocation in user defined memory regions
 *  - Memory freeing
 *
 * When disabled, library only supports allocation and does not provide any other service.
 *  - Its purpose is for memory allocation at the start of firmware initialization only
 *
 * \note            When disabled, statistics functionaltiy is not available
 *                  and only one region is supported (for now, may be updated later).
 *                  API to allocate memory remains the same as for full configuration.
 */
#define LWMEM_CFG_FULL 1

/**
 * \brief           Enables `1` or disables `0` memory cleanup on free operation (or realloc).
 *
 * It resets unused memory to `0x00` and prevents other applications seeing old data.
 * It is disabled by default since it has performance penalties.
 */
#define LWMEM_CFG_CLEAN_MEMORY 0

/**
 * \brief           Enables `1` or disables `0` statistics in the library
 *
 */
#define LWMEM_CFG_ENABLE_STATS 0

/**
 * \brief           Memory set function
 *
 * \note            Function footprint is the same as \ref memset
 */
#define LWMEM_MEMSET(dst, val, len) memset((dst), (val), (len))

/**
 * \brief           Memory copy function
 *
 * \note            Function footprint is the same as \ref memcpy
 */
#define LWMEM_MEMCPY(dst, src, len) memcpy((dst), (src), (len))

/**
 * \brief           Memory move function
 *
 * \note            Function footprint is the same as \ref memmove
 */
#define LWMEM_MEMMOVE(dst, src, len) memmove((dst), (src), (len))


#endif /* LWMEM_OPTS_HDR_H */