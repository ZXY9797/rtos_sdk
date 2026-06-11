set(MODULE_NAME app)
add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/board
        ${CMAKE_CURRENT_SOURCE_DIR}/services
        ${CMAKE_CURRENT_SOURCE_DIR}/comm
        ${CMAKE_CURRENT_SOURCE_DIR}/tasks
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${TOP_DIR}/component/ble/include
        ${TOP_DIR}/component/ble/goodix/sdk
)

target_sources(${MODULE_NAME}
    PRIVATE
        main.cc
        board/board_devices.cc
        services/ble_app.cc
        comm/link_bridge.cc
        tasks/app_tasks.cc
        ${DRIVERS_GENERATED_CC}
        ${PINCTRL_GENERATED_CC}
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        boot_common
        drivers
        osal
        -Wl,--whole-archive
        ble_gr5525
        -Wl,--no-whole-archive
)

if(TARGET link)
    target_link_libraries(${MODULE_NAME} PRIVATE link)
endif()
