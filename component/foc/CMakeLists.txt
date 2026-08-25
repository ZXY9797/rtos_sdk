set(MODULE_NAME foc)
add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME}
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
        src/svpwm.cc
        src/temperature.cc
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        include
    PRIVATE
        ${BSP_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        algo
        osal
        drivers
    PRIVATE
        soc
)
