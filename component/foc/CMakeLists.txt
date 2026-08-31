set(MODULE_NAME foc)
add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME} PRIVATE src/svpwm.cc)

target_sources_ifdef(CONFIG_FOC_CURRENT_CONTROL ${MODULE_NAME}
    PRIVATE
        src/angle_estimator.cc
        src/bldc_controller.cc
        src/data_logger.cc
        src/error_handler.cc
        src/flux_observer.cc
        src/foc_controller.cc
        src/hall_sensor.cc
        src/hfi.cc
        src/input_manager.cc
        src/math_utils.cc
        src/motor.cc
        src/motor_measurement.cc
        src/position_controller.cc
        src/temperature.cc
)

target_sources_ifdef(CONFIG_FOC_VOLTAGE_CONTROL ${MODULE_NAME}
    PRIVATE
        src/voltage_foc.cc
        src/voltage_motor.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        include
    PRIVATE
        ${BSP_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        drivers
        osal
)

if(CONFIG_FOC_CURRENT_CONTROL)
    target_link_libraries(${MODULE_NAME}
        PUBLIC
            algo
        PRIVATE
            soc)
endif()
