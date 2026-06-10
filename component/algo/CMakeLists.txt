set(MODULE_NAME algo)
add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME}
    PRIVATE
        src/biquad_filter.cc
        src/lagrange_interp.cc
        src/lead_lag_compensator.cc
        src/leso.cc
        src/ntc_sensor.cc
        src/pid_controller.cc
        src/sweep_signal.cc
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
