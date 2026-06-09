set(MODULE_NAME ble_gr5525)
add_library(${MODULE_NAME} STATIC)

# BLE SDK core
target_sources(${MODULE_NAME}
    PRIVATE
        sdk/ble.c
        src/ble_stack_impl.cc
        src/ble_hid_impl.cc
        src/ble_uart_impl.cc
        src/ble_batt_impl.cc
        src/ble_dis_impl.cc
        src/ble_scheduler.cc
)

# BLE profiles — selective inclusion to avoid compilation issues
target_sources(${MODULE_NAME}
    PRIVATE
        profiles/common/ble_prf_utils.c
        profiles/common/ble_srv_disc_utils.c
        profiles/hids/hids.c
        profiles/bas/bas.c
        profiles/dis/dis.c
        profiles/gus/gus.c
)

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
        -Wno-strict-aliasing
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-sign-compare>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ../include
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
        src
)

# libble_sdk.a contains a main.o (Goodix SDK example entry) that conflicts
# with the user's main(). Strip it from a build-dir copy before linking.
set(_BLE_SDK_ORIG ${CMAKE_CURRENT_SOURCE_DIR}/lib/libble_sdk.a)
set(_BLE_SDK_FIXED ${CMAKE_CURRENT_BINARY_DIR}/libble_sdk_fixed.a)
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
