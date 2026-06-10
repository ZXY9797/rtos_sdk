set(MODULE_NAME app)
add_library(${MODULE_NAME} ${NULL_C})

add_subdirectory(${CMAKE_SOURCE_DIR}/component/foc component_foc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
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
        foc_app.cc
        controller/speed_controller.cc
        controller/protection.cc
        controller/position_sensor.cc
        controller/pos_controller.cc
        controller/calibration.cc
        comm/can_handler.cc
        ${DRIVERS_GENERATED_CC}
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        drivers
        osal
        foc
        algo
)

# NVS
if(TARGET nvs)
    target_link_libraries(${MODULE_NAME} PRIVATE nvs)
endif()

# Link
if(TARGET link)
    target_link_libraries(${MODULE_NAME} PRIVATE link)
endif()
