set(MODULE_NAME pwm)
add_library(${MODULE_NAME} STATIC)

target_sources_ifdef(CONFIG_PWM_STM32 ${MODULE_NAME} PRIVATE pwm_stm32.cc)
target_sources_ifdef(CONFIG_PWM_GD32 ${MODULE_NAME} PRIVATE pwm_gd32.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
    PRIVATE
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        soc
        osal
)
