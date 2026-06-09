set(FREERTOS_DIR "../rtos/freertos")

# Select FreeRTOS port based on CPU core
if(CONFIG_CPU_CORTEX_M7)
    set(FREERTOS_PORT_DIR "GCC/ARM_CM7/r0p1")
elseif(CONFIG_CPU_CORTEX_M4)
    set(FREERTOS_PORT_DIR "GCC/ARM_CM4F")
elseif(CONFIG_CPU_CORTEX_M33)
    set(FREERTOS_PORT_DIR "GCC/ARM_CM33_NTZ/non_secure")
else()
    set(FREERTOS_PORT_DIR "GCC/ARM_CM7/r0p1")
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
        ${FREERTOS_DIR}/portable/${FREERTOS_PORT_DIR}/port.c
        ${FREERTOS_DIR}/portable/MemMang/heap_4.c
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        .
        ${FREERTOS_DIR}/include
        ${FREERTOS_DIR}/portable/${FREERTOS_PORT_DIR}
)

target_link_libraries(${MODULE_NAME} PUBLIC soc)
