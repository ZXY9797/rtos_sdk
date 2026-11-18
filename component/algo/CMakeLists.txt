# Pure calculations shared by firmware and host tools.
sdk_add_component(algo
    SOURCES
        src/controller/leso.cc
        src/controller/pid_controller.cc
        src/controller/pid_sys.cc
        src/filter/biquad_filter.cc
        src/filter/filter_sys.cc
        src/filter/lead_lag_compensator.cc
        src/filter/notch_peak.cc
        src/filter/transfer_function.cc
        src/math/lagrange_interp.cc
        src/math/spatial_math.cc
        src/sensor/ntc_sensor.cc
        src/signal/sweep_freq.cc
        src/signal/sweep_signal.cc
        src/trajectory/hermite_interp.cc
        src/trajectory/motion_profile.cc
        src/trajectory/s_curve_planner.cc
    INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/include")

# These algorithms are selectable in firmware and all available in host builds.
if(NOT TARGET sdk_build_config OR CONFIG_ATTITUDE_EKF)
    target_sources(algo PRIVATE src/attitude/attitude_ekf.cc)
endif()
if(NOT TARGET sdk_build_config OR CONFIG_THERMAL_CONTROL)
    target_sources(algo PRIVATE src/thermal/thermal_controller.cc)
endif()
if(NOT TARGET sdk_build_config)
    target_compile_features(algo PUBLIC cxx_std_17)
endif()
