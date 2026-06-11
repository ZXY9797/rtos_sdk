set(MODULE_NAME app)
add_library(${MODULE_NAME} ${NULL_C})

add_subdirectory(${CMAKE_SOURCE_DIR}/component/foc component_foc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/board
        ${CMAKE_CURRENT_SOURCE_DIR}/services
        ${CMAKE_CURRENT_SOURCE_DIR}/control
        ${CMAKE_CURRENT_SOURCE_DIR}/control/controller
        ${CMAKE_CURRENT_SOURCE_DIR}/comm
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/soc/gd/gd32f50x/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
        ${BSP_DIR}/soc/gd/gd32f50x
)

target_sources(${MODULE_NAME}
    PRIVATE
        main.cc
        services/app_services.cc
        board/board_devices.cc
        control/control_app.cc
        control/controller/speed_controller.cc
        control/controller/protection.cc
        control/controller/position_sensor.cc
        control/controller/pos_controller.cc
        control/controller/calibration.cc
        comm/can_handler.cc
        ${CMAKE_SOURCE_DIR}/bootloader/product/demo/common/flash_map.cc
        ${DRIVERS_GENERATED_CC}
        ${PINCTRL_GENERATED_CC}
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        boot_common
        drivers
        osal
        foc
        algo
)

if(TARGET nvs)
    target_link_libraries(${MODULE_NAME} PRIVATE nvs)
endif()

if(TARGET link)
    target_link_libraries(${MODULE_NAME} PRIVATE link)
endif()
