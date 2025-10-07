set(soc_srcs
    ${SOC_SERIES_DIR}/soc.c
    ${SOC_SERIES_DIR}/soc/system_stm32h7xx.c
)

set(soc_inc_dirs
    ${SOC_SERIES_DIR}/soc
    ${SOC_SERIES_DIR}/soc/system_stm32h7xx.c
)

target_sources(soc
    PRIVATE
        ${SOC_SERIES_DIR}/soc_stm32h7xx.c
        ${SOC_SERIES_DIR}/soc/system_stm32h7xx.c
)

target_include_directories(soc
    PUBLIC
        ${SOC_SERIES_DIR}
        ${SOC_SERIES_DIR}/soc
        ${SOC_SERIES_DIR}/drivers/include
        ${SOC_SERIES_DIR}/drivers/include/Legacy
        ${SOC_VENDER_DIR}/common
        ${SOC_VENDER_DIR}/stm32cube/common_ll/include
        ${SOC_VENDER_DIR}/stm32cube/common_ll/include
)

target_link_libraries(soc
    PUBLIC
        cmsis
)

target_compile_definitions(soc
    PUBLIC
        ${SOC_NAME_UPPER}
        SOC_FAMILY_${SOC_FAMILY_UPPER}
        SOC_SERIES_${SOC_SERIES_UPPER}
        SOC_${SOC_NAME_UPPER}
)
