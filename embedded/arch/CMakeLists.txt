set(MODULE_NAME arch)

add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
        .
    PRIVATE
        ${BSP_DIR}/include
)

file(GLOB_RECURSE ARCH_SOURCES
    ./common/*.c
    ./${CONFIG_ARCH}/core/cortex_m/*.c
    ./${CONFIG_ARCH}/core/cortex_m/*.S
)

target_sources(${MODULE_NAME}
    PRIVATE
        ${ARCH_SOURCES}
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        soc
)