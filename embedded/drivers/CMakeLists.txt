set(MODULE_NAME drivers)
add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
)

add_subdirectory_ifdef(CONFIG_GPIO gpio)
target_link_libraries_ifdef(CONFIG_GPIO ${MODULE_NAME} PUBLIC gpio)

add_subdirectory_ifdef(CONFIG_SPI spi)
target_link_libraries_ifdef(CONFIG_SPI ${MODULE_NAME} PUBLIC spi)

add_subdirectory_ifdef(CONFIG_I2C i2c)
target_link_libraries_ifdef(CONFIG_I2C ${MODULE_NAME} PUBLIC i2c)

add_subdirectory_ifdef(CONFIG_UART uart)
target_link_libraries_ifdef(CONFIG_UART ${MODULE_NAME} PUBLIC uart)

add_subdirectory_ifdef(CONFIG_CAN can)
target_link_libraries_ifdef(CONFIG_CAN ${MODULE_NAME} PUBLIC can)

add_subdirectory_ifdef(CONFIG_FLASH flash)
target_link_libraries_ifdef(CONFIG_FLASH ${MODULE_NAME} PUBLIC flash)

add_subdirectory_ifdef(CONFIG_PWM pwm)
target_link_libraries_ifdef(CONFIG_PWM ${MODULE_NAME} PUBLIC pwm)

add_subdirectory_ifdef(CONFIG_ADC adc)
target_link_libraries_ifdef(CONFIG_ADC ${MODULE_NAME} PUBLIC adc)

add_subdirectory_ifdef(CONFIG_DMA dma)
target_link_libraries_ifdef(CONFIG_DMA ${MODULE_NAME} PUBLIC dma)

add_subdirectory_ifdef(CONFIG_IMU_ICM40609D imu)

add_subdirectory(log)

add_subdirectory(interrupt_controller)
add_subdirectory(clock_control)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        interrupt_controller
        clock_control
        log
)

