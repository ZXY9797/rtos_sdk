add_library(position_sensor STATIC)

target_sources(position_sensor
    PRIVATE
        src/hall_calibration.cc
        src/hall_sensor.cc)
target_include_directories(position_sensor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(position_sensor
    PUBLIC
        control_contracts
        drivers)
