#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

#include <soc.h>

/* System */
#define configUSE_PREEMPTION                     1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION  1
#define configCPU_CLOCK_HZ                       SystemCoreClock
#define configTICK_RATE_HZ                       1000
#define configMAX_PRIORITIES                     32
#define configMINIMAL_STACK_SIZE                 128
#define configMAX_TASK_NAME_LEN                  16
#define configUSE_16_BIT_TICKS                   0
#define configIDLE_SHOULD_YIELD                  1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES    4
#define configQUEUE_REGISTRY_SIZE                0

/* Memory */
#define configSUPPORT_STATIC_ALLOCATION          1
#define configSUPPORT_DYNAMIC_ALLOCATION         1
#define configTOTAL_HEAP_SIZE                    (32 * 1024)

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

/* Interrupt nesting - STM32H7 has 4 priority bits */
#define configPRIO_BITS                          4
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5
#define configKERNEL_INTERRUPT_PRIORITY         (configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))
#define configMAX_SYSCALL_INTERRUPT_PRIORITY    (configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS))

/* Assert */
#define configASSERT(x) if (!(x)) { taskDISABLE_INTERRUPTS(); for(;;); }

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
