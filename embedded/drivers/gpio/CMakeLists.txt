set(MODULE_NAME gpio)
add_library(${MODULE_NAME} STATIC)

# STM32 GPIO 实现
target_sources_ifdef(CONFIG_GPIO_STM32 ${MODULE_NAME} PRIVATE gpio_stm32.cc)

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