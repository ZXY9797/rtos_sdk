set(MODULE_NAME app)
add_library(${MODULE_NAME} ${NULL_C})

if(CONFIG_GIMBAL_MOTOR_CURRENT_FOC)
    message(FATAL_ERROR
        "The gimbal reference board has no synchronized phase-current "
        "power-stage adapter; select CONFIG_GIMBAL_MOTOR_VOLTAGE_FOC")
endif()

if(CONFIG_GIMBAL_PHASE_CURRENT_SENSE
   OR CONFIG_GIMBAL_VBUS_SENSE
   OR CONFIG_GIMBAL_DRIVER_TEMP_SENSE
   OR CONFIG_GIMBAL_MOS_TEMP_SENSE)
    message(FATAL_ERROR
        "Optional gimbal telemetry was enabled without board sampling "
        "adapters; add the DTS/driver implementation before enabling it")
endif()

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/board
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/services
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/soc/gd/gd32f50x/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
        ${BSP_DIR}/soc/gd/gd32f50x)

target_sources(${MODULE_NAME}
    PRIVATE
        main.cc
        board/board_devices.cc
        services/gimbal_app.cc
        services/parameters.cc
        services/parameter_store.cc
        ${DRIVERS_GENERATED_CC}
        ${PINCTRL_GENERATED_CC})

target_link_libraries(${MODULE_NAME}
    PRIVATE
        boot_common
        attitude_estimator
        control_contracts
        drivers
        feedback_control
        foc
        ipc
        motion
        osal
        nvs
        position_sensor
        safety_supervisor
        thermal_control)
