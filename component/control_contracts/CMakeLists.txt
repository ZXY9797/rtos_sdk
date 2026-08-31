add_library(control_contracts INTERFACE)

target_include_directories(control_contracts
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(control_contracts INTERFACE algo)
