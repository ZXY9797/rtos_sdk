set(MODULE_NAME clock_control)
add_library(${MODULE_NAME} ${NULL_C})

# STM32 时钟实现
target_sources_ifdef(CONFIG_CLOCK_CONTROL_STM32_CUBE ${MODULE_NAME} PRIVATE clock_stm32.cc)

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
