set(MODULE_NAME system)

add_library(${MODULE_NAME} OBJECT)
add_library(system_fault OBJECT)

target_sources(${MODULE_NAME}
    PRIVATE
        init.cc
        assert.cc
        device_pm.cc
        sensor_core.cc
        watchdog.cc
)
target_sources(system_fault PRIVATE fault.cc syscalls.c)

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
)
target_link_libraries(system_fault
    PRIVATE
        osal
        soc
)

if(CONFIG_APP_WATCHDOG)
    set(APP_WATCHDOG_SOURCE "${PROJECT_DIR}/common/app_watchdog.cc")
    if(NOT EXISTS "${APP_WATCHDOG_SOURCE}")
        message(FATAL_ERROR
            "CONFIG_APP_WATCHDOG requires product provider: ${APP_WATCHDOG_SOURCE}")
    endif()
    target_sources(${MODULE_NAME} PRIVATE "${APP_WATCHDOG_SOURCE}")
endif()
