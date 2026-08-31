add_library(motion STATIC)

target_sources(motion
    PRIVATE
        src/dynamics.cc
        src/kinematics.cc
        src/motion_planner.cc)

target_include_directories(motion
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(motion PUBLIC algo control_contracts)
