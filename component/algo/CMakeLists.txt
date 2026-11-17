sdk_add_component(algo
    SOURCES
        src/biquad_filter.cc
        src/lagrange_interp.cc
        src/lead_lag_compensator.cc
        src/leso.cc
        src/motion_profile.cc
        src/ntc_sensor.cc
        src/pid_controller.cc
        src/spatial_math.cc
        src/sweep_signal.cc
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include")
