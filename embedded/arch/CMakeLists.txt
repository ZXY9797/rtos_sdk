set(MODULE_NAME arch)

add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
        .
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
        common/include
)

file(GLOB_RECURSE ARCH_SOURCES
    ./common/*.c
    ./${CONFIG_ARCH}/core/cortex_m/*.c
    ./${CONFIG_ARCH}/core/cortex_m/*.cc
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

# target_compile_options(${MODULE_NAME}
#     PRIVATE
#         $<$<COMPILE_LANGUAGE:ASM>:-D_ASMLANGUAGE>
# )