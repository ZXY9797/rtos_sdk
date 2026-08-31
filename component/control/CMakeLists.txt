add_library(feedback_control STATIC)

target_sources(feedback_control PRIVATE src/controller.cc)
target_include_directories(feedback_control
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(feedback_control
    PUBLIC
        algo
        control_contracts
        motion)
