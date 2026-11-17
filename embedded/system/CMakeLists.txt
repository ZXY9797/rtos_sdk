set(MODULE_NAME system)

sdk_add_library(${MODULE_NAME} OBJECT)
sdk_add_library(system_fault OBJECT)

target_sources(${MODULE_NAME}
    PRIVATE
        init.cc
        assert.cc
        device_pm.cc
        sensor_core.cc
        watchdog.cc
        dma_memory.cc
        diagnostics.cc
        clock.cc
)
target_sources(system_fault PRIVATE fault.cc syscalls.c output_safety.cc)

target_include_directories(${MODULE_NAME}
    PRIVATE
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
)
target_include_directories(system_fault
    PRIVATE
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        osal
        soc
        runtime
        boot_common
)
target_link_libraries(system_fault
    PRIVATE
        osal
        soc
        runtime
)

if(CONFIG_APP_WATCHDOG)
    set(APP_WATCHDOG_SOURCE "${PROJECT_DIR}/common/app_watchdog.cc")
    if(NOT EXISTS "${APP_WATCHDOG_SOURCE}")
        message(FATAL_ERROR
            "CONFIG_APP_WATCHDOG requires product provider: ${APP_WATCHDOG_SOURCE}")
    endif()
    target_sources(${MODULE_NAME} PRIVATE "${APP_WATCHDOG_SOURCE}")
endif()

if(CONFIG_DMA_CACHED_MEMORY)
    set(_dma_provider "${PROJECT_DIR}/common/dma_memory.cc")
    if(NOT EXISTS "${_dma_provider}")
        message(FATAL_ERROR "Cached DMA requires product memory provider: ${_dma_provider}")
    endif()
    target_sources(${MODULE_NAME} PRIVATE "${_dma_provider}")
endif()
