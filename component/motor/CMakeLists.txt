sdk_add_component(motor
    SOURCES
        src/calibration/backlash.cc
        src/calibration/calibration.cc
        src/calibration/calibration_record.cc
        src/foc/current_loop.cc
        src/foc/model_identification.cc
        src/foc/svpwm.cc
        src/foc/svpwm_sector.cc
        src/foc/transform.cc
        src/foc/voltage_foc.cc
        src/interaction/joint_interaction.cc
        src/motor/diagnostics.cc
        src/motor/execution_limits.cc
        src/motor/feedback.cc
        src/motor/feedback_processor.cc
        src/motor/joint_controller.cc
        src/motor/joint_target.cc
        src/motor/parameters.cc
        src/motor/protection.cc
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include"
    REQUIRES sdk::algo)

if(NOT TARGET sdk_build_config OR CONFIG_POSITION_SENSOR)
    target_sources(motor PRIVATE src/sensor/hall_sensor.cc src/sensor/hall_calibration.cc)
endif()

if(NOT TARGET sdk_build_config OR CONFIG_FOC_CURRENT_CONTROL)
    target_sources(motor PRIVATE
        src/foc/angle_estimator.cc
        src/foc/bldc_controller.cc
        src/foc/data_logger.cc
        src/foc/error_handler.cc
        src/foc/flux_observer.cc
        src/foc/foc_controller.cc
        src/foc/hall_commutation_sensor.cc
        src/foc/hfi.cc
        src/foc/input_manager.cc
        src/foc/math_utils.cc
        src/foc/motor_measurement.cc
        src/foc/position_controller.cc
        src/foc/temperature.cc)
endif()
