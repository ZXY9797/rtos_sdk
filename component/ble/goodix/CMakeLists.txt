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

# BLE profiles - add all that have .c files
file(GLOB_RECURSE BLE_PROFILE_SOURCES "profiles/*.c")
target_sources(${MODULE_NAME} PRIVATE ${BLE_PROFILE_SOURCES})

# BLE libraries - add all that have .c files
file(GLOB_RECURSE BLE_LIB_SOURCES "libraries/ble/*.c")
target_sources(${MODULE_NAME} PRIVATE ${BLE_LIB_SOURCES})

# Essential libraries
file(GLOB_RECURSE APP_LIB_SOURCES
    "libraries/app_timer/*.c"
    "libraries/app_scheduler/*.c"
    "libraries/app_log/*.c"
    "libraries/app_error/*.c"
    "libraries/app_key/*.c"
    "libraries/ring_buffer/*.c"
    "libraries/hal_flash/*.c"
    "libraries/app_memory/*.c"
    "libraries/app_queue/*.c"
    "libraries/app_linked_list/*.c"
    "libraries/app_fds/*.c"
    "libraries/utility/*.c"
    "libraries/sensorsim/*.c"
)
target_sources(${MODULE_NAME} PRIVATE ${APP_LIB_SOURCES})

target_include_directories(${MODULE_NAME}
    PUBLIC
        ../include
        sdk
        profiles
        profiles/common
        libraries
        libraries/ble
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
        libraries/app_fds
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

# Link precompiled BLE library
target_link_libraries(${MODULE_NAME}
    PUBLIC
        osal
        soc
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libble_sdk.a
)
