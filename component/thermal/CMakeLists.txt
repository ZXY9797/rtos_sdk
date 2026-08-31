add_library(thermal_control STATIC)

target_sources(thermal_control PRIVATE src/thermal_controller.cc)
target_include_directories(thermal_control
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(thermal_control PUBLIC control_contracts)
