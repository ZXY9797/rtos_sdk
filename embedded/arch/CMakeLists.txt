set(MODULE_NAME arch)

add_library(${MODULE_NAME} OBJECT)

target_include_directories(${MODULE_NAME}
    PUBLIC
        .
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
        common/include
        $<TARGET_PROPERTY:soc,INTERFACE_INCLUDE_DIRECTORIES>
)

target_compile_definitions(${MODULE_NAME}
    PRIVATE
        $<TARGET_PROPERTY:soc,INTERFACE_COMPILE_DEFINITIONS>
)

target_sources(${MODULE_NAME}
    PRIVATE
        ${CONFIG_ARCH}/core/cortex_m/arch_func.S
        ${CONFIG_ARCH}/core/cortex_m/cache.c
        ${CONFIG_ARCH}/core/cortex_m/fault.S
        ${CONFIG_ARCH}/core/cortex_m/irq_manage.c
        ${CONFIG_ARCH}/core/cortex_m/prep_c.c
        ${CONFIG_ARCH}/core/cortex_m/reset.S
        ${CONFIG_ARCH}/core/cortex_m/sct_load.c
        ${CONFIG_ARCH}/core/cortex_m/vector_table.S
)

# target_compile_options(${MODULE_NAME}
#     PRIVATE
#         $<$<COMPILE_LANGUAGE:ASM>:-D_ASMLANGUAGE>
# )
