set(MODULE_NAME embedded)
sdk_add_library(${MODULE_NAME} INTERFACE)
add_library(sdk::platform ALIAS ${MODULE_NAME})

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
        runtime
        arch
        soc
        drivers
        modules
        osal
        $<$<STREQUAL:${FIRMWARE_TYPE},app>:system>
)
