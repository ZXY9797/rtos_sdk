set(FREERTOS_DIR "../rtos/freertos")

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
        ${FREERTOS_DIR}/portable/GCC/ARM_CM7/r0p1/port.c
        ${FREERTOS_DIR}/portable/MemMang/heap_4.c
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        .
        ${FREERTOS_DIR}/include
        ${FREERTOS_DIR}/portable/GCC/ARM_CM7/r0p1
)

target_link_libraries(${MODULE_NAME} PUBLIC soc)
