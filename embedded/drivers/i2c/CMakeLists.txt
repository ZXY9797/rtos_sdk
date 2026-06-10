set(MODULE_NAME i2c)
add_library(${MODULE_NAME} STATIC)

target_sources_ifdef(CONFIG_I2C_STM32 ${MODULE_NAME} PRIVATE i2c_stm32.cc)

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
