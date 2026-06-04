set(MODULE_NAME interrupt_controller)
add_library(${MODULE_NAME} STATIC ${NULL_C})

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

# STM32 中断控制器 C 实现 (硬件层)
target_sources_ifdef(CONFIG_EXTI_STM32 ${MODULE_NAME} PRIVATE intc_exti_stm32.c)
target_sources_ifdef(CONFIG_GPIO_INTC_STM32 ${MODULE_NAME} PRIVATE intc_gpio_stm32.c)

# C++ 包装层
target_sources_ifdef(CONFIG_EXTI_STM32 ${MODULE_NAME} PRIVATE exti_stm32.cc)
target_sources_ifdef(CONFIG_GPIO_INTC_STM32 ${MODULE_NAME} PRIVATE gpio_intc_stm32.cc)