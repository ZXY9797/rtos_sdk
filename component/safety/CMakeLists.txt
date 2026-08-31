add_library(safety_supervisor STATIC)

target_sources(safety_supervisor PRIVATE src/safety_manager.cc)
target_include_directories(safety_supervisor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(safety_supervisor PUBLIC control_contracts)
