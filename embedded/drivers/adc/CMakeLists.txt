set(MODULE_NAME adc)
add_library(${MODULE_NAME} STATIC)

target_sources_ifdef(CONFIG_ADC_GD32 ${MODULE_NAME} PRIVATE adc_gd32.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
    PRIVATE
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        soc
)
