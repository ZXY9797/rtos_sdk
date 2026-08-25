set(FREERTOS_DIR "${CMAKE_CURRENT_LIST_DIR}/../rtos/freertos")

# Select FreeRTOS port based on CPU core
if(CONFIG_CPU_CORTEX_M7)
    set(FREERTOS_PORT_DIR "GCC/ARM_CM7/r0p1")
elseif(CONFIG_CPU_CORTEX_M4)
    set(FREERTOS_PORT_DIR "GCC/ARM_CM4F")
elseif(CONFIG_CPU_CORTEX_M33)
    set(FREERTOS_PORT_DIR "GCC/ARM_CM33_NTZ/non_secure")
else()
    message(FATAL_ERROR
        "FreeRTOS has no validated port for the selected CPU core")
endif()

set(FREERTOS_PORT_SOURCES
    ${FREERTOS_DIR}/portable/${FREERTOS_PORT_DIR}/port.c
)

# portasm.c exists only in newer ARMv8M ports, not in legacy CM4F/CM7
if(EXISTS ${FREERTOS_DIR}/portable/${FREERTOS_PORT_DIR}/portasm.c)
    list(APPEND FREERTOS_PORT_SOURCES
        ${FREERTOS_DIR}/portable/${FREERTOS_PORT_DIR}/portasm.c
    )
endif()

target_sources(${MODULE_NAME}
    PRIVATE
        osal.cc
        mem.cc
        ${FREERTOS_DIR}/tasks.c
        ${FREERTOS_DIR}/queue.c
        ${FREERTOS_DIR}/list.c
        ${FREERTOS_DIR}/timers.c
        ${FREERTOS_DIR}/event_groups.c
        ${FREERTOS_DIR}/stream_buffer.c
        ${FREERTOS_PORT_SOURCES}
        ${FREERTOS_DIR}/portable/MemMang/heap_4.c
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        .
        ${FREERTOS_DIR}/include
        ${FREERTOS_DIR}/portable/${FREERTOS_PORT_DIR}
)

target_link_libraries(${MODULE_NAME} PUBLIC soc)

if(CONFIG_OSAL_TICKLESS_IDLE)
    set(OSAL_LOW_POWER_SOURCE "${PROJECT_DIR}/common/osal_low_power.cc")
    if(NOT EXISTS "${OSAL_LOW_POWER_SOURCE}")
        message(FATAL_ERROR
            "CONFIG_OSAL_TICKLESS_IDLE requires product provider: ${OSAL_LOW_POWER_SOURCE}")
    endif()
    target_sources(${MODULE_NAME} PRIVATE "${OSAL_LOW_POWER_SOURCE}")
endif()
