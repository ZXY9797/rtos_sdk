set(MODULE_NAME gimbal_core)
add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME}
    PRIVATE
        src/attitude_ekf.cc
        src/controller.cc
    src/dynamics.cc
    src/hall_calibration.cc
    src/hall_sensor.cc
        src/kinematics.cc
        src/math.cc
        src/motion_planner.cc
        src/parameters.cc
        src/safety_manager.cc
        src/thermal_controller.cc
        src/voltage_foc.cc
        src/voltage_motor.cc
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        osal
        drivers
)
