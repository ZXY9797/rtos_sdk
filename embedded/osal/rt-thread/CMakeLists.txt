set(RTOS_DIR "../rtos/rt-thread")

target_sources(${MODULE_NAME} PRIVATE osal.c cpu_usage.c)
target_sources(${MODULE_NAME}
    PRIVATE
        ${RTOS_DIR}/src/clock.c
        ${RTOS_DIR}/src/components.c
        ${RTOS_DIR}/src/cpu.c
        ${RTOS_DIR}/src/device.c
        ${RTOS_DIR}/src/idle.c
        ${RTOS_DIR}/src/ipc.c
        ${RTOS_DIR}/src/irq.c
        ${RTOS_DIR}/src/kservice.c
        ${RTOS_DIR}/src/mem.c
        ${RTOS_DIR}/src/memheap.c
        ${RTOS_DIR}/src/mempool.c
        ${RTOS_DIR}/src/object.c
        ${RTOS_DIR}/src/scheduler.c
        ${RTOS_DIR}/src/signal.c
        ${RTOS_DIR}/src/slab.c
        ${RTOS_DIR}/src/thread.c
        ${RTOS_DIR}/src/timer.c
)

target_sources_ifdef(CONFIG_CPU_CORTEX_M7 ${MODULE_NAME}
    PRIVATE
        ${RTOS_DIR}/libcpu/arm/cortex-m7/context_gcc.S
        ${RTOS_DIR}/libcpu/arm/cortex-m7/cpuport.c
        ${RTOS_DIR}/libcpu/arm/cortex-m7/cpu_cache.c
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        .
        ${RTOS_DIR}/include
)

target_link_libraries(${MODULE_NAME} PRIVATE soc)
