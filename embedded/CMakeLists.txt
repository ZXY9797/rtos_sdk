set(MODULE_NAME embedded)
add_library(${MODULE_NAME} INTERFACE)

target_include_directories(${MODULE_NAME}
    INTERFACE
        ./include
)

add_subdirectory(soc)
add_subdirectory(drivers)
add_subdirectory(modules)
add_subdirectory(arch)
add_subdirectory(osal)
add_subdirectory(system)

target_link_libraries(${MODULE_NAME}
    INTERFACE
        arch
        soc
        drivers
        modules
        osal
        $<$<STREQUAL:${FIRMWARE_TYPE},app>:system>
)
