set(MODULE_NAME ble_gr5525)
add_library(${MODULE_NAME} STATIC)

# BLE SDK core
target_sources(${MODULE_NAME}
    PRIVATE
        sdk/ble.c
        src/ble_stack_impl.cc
)

# BLE profiles — selective inclusion to avoid compilation issues
target_sources(${MODULE_NAME}
    PRIVATE
        profiles/common/ble_prf_utils.c
        profiles/common/ble_srv_disc_utils.c
)

# Disabled services do not add either their wrapper or vendor profile.
if(CONFIG_BLE_HID)
    target_sources(${MODULE_NAME} PRIVATE
        src/ble_hid_impl.cc
        profiles/hids/hids.c)
endif()

if(CONFIG_BLE_UART_SERVICE)
    target_sources(${MODULE_NAME} PRIVATE
        src/ble_uart_impl.cc
        profiles/gus/gus.c)
endif()

if(CONFIG_BLE_BATTERY_SERVICE)
    target_sources(${MODULE_NAME} PRIVATE
        src/ble_batt_impl.cc
        profiles/bas/bas.c)
endif()

if(CONFIG_BLE_DEVICE_INFO)
    target_sources(${MODULE_NAME} PRIVATE
        src/ble_dis_impl.cc
        profiles/dis/dis.c)
endif()

# BLE libraries — only ble_time needed
target_sources(${MODULE_NAME}
    PRIVATE
        libraries/ble/ble_time/ble_time.c
)

# Essential app libraries (excluding problematic ones)
target_sources(${MODULE_NAME}
    PRIVATE
        libraries/app_timer/app_timer.c
        libraries/app_scheduler/app_scheduler.c
        libraries/app_log/app_log.c
        libraries/app_error/app_error.c
        libraries/app_key/app_key.c
        libraries/ring_buffer/ring_buffer.c
        libraries/hal_flash/hal_flash.c
        libraries/app_memory/app_memory.c
        libraries/app_queue/app_queue.c
        libraries/app_linked_list/app_linked_list.c
        libraries/utility/utility.c
        libraries/sensorsim/sensorsim.c
)

# Warning suppression for BLE SDK / GR5525 HAL code
target_compile_options(${MODULE_NAME}
    PRIVATE
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-sign-compare>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=sign-compare>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=register>
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ../include
    PRIVATE
        src
)

# Keep the public wrapper warning-clean while classifying the imported Goodix
# SDK and HAL include trees as third-party system headers.
target_include_directories(${MODULE_NAME} SYSTEM
    PUBLIC
        sdk
        profiles
        profiles/common
        profiles/hids
        profiles/bas
        profiles/dis
        profiles/gus
        libraries
        libraries/ble
        libraries/ble/ble_time
        libraries/app_timer
        libraries/app_scheduler
        libraries/app_log
        libraries/app_error
        libraries/app_key
        libraries/ring_buffer
        libraries/hal_flash
        libraries/app_memory
        libraries/app_queue
        libraries/app_linked_list
        libraries/utility
        libraries/sensorsim
        ${BSP_DIR}/soc/goodix/gr5525x/include
        ${BSP_DIR}/soc/goodix/gr5525x/include/hal
        ${BSP_DIR}/soc/goodix/gr5525x/include/app
        ${BSP_DIR}/soc/goodix/gr5525x/include/platform
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/soc/goodix/gr5525x/include/cmsis
)

# libble_sdk.a contains a main.o (Goodix SDK example entry) that conflicts
# with the user's main(). Strip it from a build-dir copy before linking.
set(_BLE_SDK_ORIG ${CMAKE_CURRENT_SOURCE_DIR}/lib/libble_sdk.a)
set(_BLE_SDK_FIXED ${CMAKE_CURRENT_BINARY_DIR}/libble_sdk_fixed.a)
set(_BLE_SDK_SHA256
    9f3a6feb733d691962aa44039d26187adc28ed6877fda599208eb0ee7b450e5b)
if(NOT EXISTS ${_BLE_SDK_ORIG})
    message(FATAL_ERROR
        "Missing tracked Goodix SDK v1.0.3_patch_2 library: "
        "${_BLE_SDK_ORIG}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_BLE_SDK_ORIG})
file(SHA256 ${_BLE_SDK_ORIG} _ble_sdk_sha256)
if(NOT _ble_sdk_sha256 STREQUAL _BLE_SDK_SHA256)
    message(FATAL_ERROR
        "libble_sdk.a is incompatible with the GR5525 ROM symbol table: "
        "expected Goodix SDK v1.0.3_patch_2 SHA-256 ${_BLE_SDK_SHA256}, "
        "got ${_ble_sdk_sha256}")
endif()
add_custom_command(
    OUTPUT ${_BLE_SDK_FIXED}
    COMMAND ${CMAKE_COMMAND} -E copy ${_BLE_SDK_ORIG} ${_BLE_SDK_FIXED}
    COMMAND ${CMAKE_AR} d ${_BLE_SDK_FIXED} main.o
    DEPENDS ${_BLE_SDK_ORIG}
    COMMENT "Stripping main.o from libble_sdk.a"
)
add_custom_target(ble_sdk_fix DEPENDS ${_BLE_SDK_FIXED})

# Link precompiled BLE library with --whole-archive (circular references)
# osal/soc: PRIVATE to avoid double-listing (embedded already links them)
target_link_libraries(${MODULE_NAME}
    PRIVATE
        osal
        soc
        -Wl,--whole-archive
        ${_BLE_SDK_FIXED}
        -Wl,--no-whole-archive
)
add_dependencies(${MODULE_NAME} ble_sdk_fix)
