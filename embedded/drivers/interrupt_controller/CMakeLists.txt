set(MODULE_NAME interrupt_controller)
add_library(${MODULE_NAME} irq_controller_api.c)

target_include_directories(${MODULE_NAME}
    PUBLIC
    PRIVATE
        .
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        cmsis
        soc
        arch
)

target_sources_ifdef(CONFIG_GPIO_INTC_STM32 ${MODULE_NAME} PRIVATE intc_gpio_stm32.c)
target_sources_ifdef(CONFIG_EXTI_STM32 ${MODULE_NAME} PRIVATE intc_exti_stm32.c)