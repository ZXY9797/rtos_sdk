set(MODULE_NAME system)
add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
    PRIVATE
        ${BSP_DIR}/include
)

target_sources(${MODULE_NAME}
    PRIVATE
        ./device.c
        ./init.c
        ./mmem.c
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        drivers
        soc
        kernel
)

