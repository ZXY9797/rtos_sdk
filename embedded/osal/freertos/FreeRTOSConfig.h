#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <soc.h>

/* ARMv8-M specific */
#define configENABLE_FPU                         1
#define configENABLE_MPU                         0
#define configENABLE_TRUSTZONE                   0

/* System */
#define configUSE_PREEMPTION                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configCPU_CLOCK_HZ                       SystemCoreClock
#define configTICK_RATE_HZ                       CONFIG_SYS_CLOCK_TICKS_PER_SEC
#define configMAX_PRIORITIES                     32
#define configMINIMAL_STACK_SIZE                 128
#define configMAX_TASK_NAME_LEN                  16
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES    4
#define configQUEUE_REGISTRY_SIZE                0
#if defined(CONFIG_OSAL_TICKLESS_IDLE)
#define configUSE_TICKLESS_IDLE                  1
#else
#define configUSE_TICKLESS_IDLE                  0
#endif

/* Memory */
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configTOTAL_HEAP_SIZE                    CONFIG_FREERTOS_HEAP_SIZE

/* Hook functions */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      0
#define configCHECK_FOR_STACK_OVERFLOW           2
#define configUSE_MALLOC_FAILED_HOOK             1
#define configUSE_DAEMON_TASK_STARTUP_HOOK       0

/* Runtime stats */
#define configGENERATE_RUN_TIME_STATS            0
#define configUSE_TRACE_FACILITY                 1
#define configUSE_STATS_FORMATTING_FUNCTIONS     0

/* Co-routine (unused) */
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          2

/* Software timer */
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                (configMAX_PRIORITIES - 1)
#define configTIMER_QUEUE_LENGTH                 10
#define configTIMER_TASK_STACK_DEPTH             256
#define configUSE_STREAM_BUFFERS                 1

/* Mutex / Semaphore */
#define configUSE_MUTEXES                        1
#define configUSE_RECURSIVE_MUTEXES              0
#define configUSE_COUNTING_SEMAPHORES            1

/* Interrupt priorities are derived from the selected SoC CMSIS contract. */
#define configPRIO_BITS                          CONFIG_NUM_IRQ_PRIO_BITS
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY \
    ((1UL << configPRIO_BITS) - 1UL)
#if configPRIO_BITS == 8
/* CM4 PRIGROUP always consumes bit 0 when all eight bits are implemented. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    4
#else
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#endif
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

#if (configPRIO_BITS == 0) || (configPRIO_BITS > 8)
#error "Invalid CONFIG_NUM_IRQ_PRIO_BITS for the FreeRTOS Cortex-M port"
#endif
#if configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY \
    >= configLIBRARY_LOWEST_INTERRUPT_PRIORITY
#error "FreeRTOS syscall interrupt priority leaves no callable IRQ range"
#endif

/* Route kernel contract violations through the persistent fatal path. */
#ifdef __cplusplus
extern "C" {
#endif
void osal_freertos_assert_failed(const char *file, unsigned int line);
#ifdef __cplusplus
}
#endif
#define configASSERT(x) do { \
    if (!(x)) { osal_freertos_assert_failed(__FILE__, __LINE__); } \
} while (0)

/* Map FreeRTOS port interrupt handlers to SDK vector table names */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
#define xPortSysTickHandler SysTick_Handler

/* Optional functions */
#define INCLUDE_vTaskPrioritySet             1
#define INCLUDE_uxTaskPriorityGet            1
#define INCLUDE_vTaskDelete                  1
#define INCLUDE_vTaskSuspend                 1
#define INCLUDE_xResumeFromISR               0
#define INCLUDE_vTaskDelayUntil              1
#define INCLUDE_vTaskDelay                   1
#define INCLUDE_xTaskGetSchedulerState       1
#define INCLUDE_xTaskGetCurrentTaskHandle    1
#define INCLUDE_uxTaskGetStackHighWaterMark  1
#define INCLUDE_eTaskGetState                1
#define INCLUDE_xTimerPendFunctionCall       1
#define INCLUDE_xTaskAbortDelay              1
#define INCLUDE_xTaskGetHandle               0

/* Redirect FreeRTOS memory allocation to pvPortFree/pvPortMalloc (default) */
/* These are already provided by heap_4.c */

#endif /* FREERTOS_CONFIG_H */
