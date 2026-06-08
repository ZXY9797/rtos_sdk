set(MODULE_NAME embedded)
add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
        ./include
    PRIVATE
)

add_subdirectory(soc)
add_subdirectory(drivers)
add_subdirectory(modules)
add_subdirectory(arch)
add_subdirectory(osal)
add_subdirectory(system)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        -Wl,--whole-archive
        arch
        soc
        drivers
        modules
        osal
        system
        -Wl,--no-whole-archive
)
