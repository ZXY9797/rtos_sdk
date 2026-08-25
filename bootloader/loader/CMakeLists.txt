cmake_minimum_required(VERSION 3.20)

set(MODULE_NAME app)
# Loader sources contain initcall-only translation units. An object library
# makes their inclusion deterministic; a static archive could silently drop
# them when no ordinary symbol referenced the object.
add_library(${MODULE_NAME} OBJECT ${NULL_C})
target_compile_options(${MODULE_NAME} PRIVATE
    -fstack-usage
    -Wstack-usage=768)

if(NOT CONFIG_RTOS_BAREMETAL)
    message(FATAL_ERROR
        "Loader requires CONFIG_RTOS_BAREMETAL=y; no RTOS scheduler is started")
endif()
if(NOT CONFIG_OSAL_BAREMETAL_HEAP_SIZE EQUAL 0)
    message(FATAL_ERROR
        "Loader forbids dynamic allocation; set CONFIG_OSAL_BAREMETAL_HEAP_SIZE=0")
endif()
if(CONFIG_BOOT_PRODUCTION AND CONFIG_BOOT_MODE_DIRECT)
    message(FATAL_ERROR
        "Production loader requires sector-swap mode for rollback recovery")
endif()
if(CONFIG_BOOT_PRODUCTION AND NOT CONFIG_BOOT_DFU_CUSTOM)
    message(FATAL_ERROR
        "Production loader requires an authenticated product DFU transport")
endif()

set(BOOT_SECURITY_SOURCE ${PROJECT_BOOT_COMMON_DIR}/boot_security.cc)
if(CONFIG_BOOT_PRODUCTION AND NOT EXISTS "${BOOT_SECURITY_SOURCE}")
    message(FATAL_ERROR
        "Production boot requires product security provider: ${BOOT_SECURITY_SOURCE}")
endif()

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
        ${TOP_DIR}/component/link/include
        ${PROJECT_PRODUCT_DIR}/include
)

target_sources(${MODULE_NAME}
    PRIVATE
        src/image_check.cc
        src/boot_logic.cc
        src/jump_app.cc
        src/boot_main.cc
        src/boot_init.cc
        src/runtime.cc
        upgrade_logic/upgrade_state.cc
        upgrade_logic/flash_copy.cc
        protocol/protocol.cc
        ${TOP_DIR}/component/link/src/codec.cc
        ${TOP_DIR}/component/link/src/crc16.cc
        ${DRIVERS_GENERATED_CC}
        ${PINCTRL_GENERATED_CC}
)

if(CONFIG_BOOT_DFU_UART)
    target_sources(${MODULE_NAME} PRIVATE transport.cc)
elseif(CONFIG_BOOT_DFU_CUSTOM)
    set(BOOT_TRANSPORT_SOURCE
        ${PROJECT_BOOT_COMMON_DIR}/boot_transport.cc)
    if(NOT EXISTS "${BOOT_TRANSPORT_SOURCE}")
        message(FATAL_ERROR
            "Custom DFU transport requires: ${BOOT_TRANSPORT_SOURCE}")
    endif()
    target_sources(${MODULE_NAME} PRIVATE ${BOOT_TRANSPORT_SOURCE})
else()
    message(FATAL_ERROR "Loader requires one DFU recovery transport")
endif()

if(EXISTS "${BOOT_SECURITY_SOURCE}")
    target_sources(${MODULE_NAME} PRIVATE ${BOOT_SECURITY_SOURCE})
endif()

if(CONFIG_BOOT_WATCHDOG)
    set(BOOT_WATCHDOG_SOURCE
        ${PROJECT_BOOT_COMMON_DIR}/boot_watchdog.cc)
    if(NOT EXISTS "${BOOT_WATCHDOG_SOURCE}")
        message(FATAL_ERROR
            "Watchdog-enabled loader requires: ${BOOT_WATCHDOG_SOURCE}")
    endif()
    target_sources(${MODULE_NAME} PRIVATE ${BOOT_WATCHDOG_SOURCE})
endif()

target_link_libraries(${MODULE_NAME}
    PRIVATE
        boot_common
        drivers
        osal
        soc
)
