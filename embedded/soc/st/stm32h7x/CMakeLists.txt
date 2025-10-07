set(STM32_HAL_LIB_DIR ${SOC_VENDER_DIR}/stm32cube/stm32h7xx)

target_include_directories(${MODULE_NAME}
  PUBLIC
    .
    ${STM32_HAL_LIB_DIR}/drivers/include
    ${STM32_HAL_LIB_DIR}/soc
    ${SOC_VENDER_DIR}/stm32cube/common_ll/include
)

target_sources(${MODULE_NAME} PRIVATE soc_stm32h7xx.c)
target_sources(${MODULE_NAME} PRIVATE ${STM32_HAL_LIB_DIR}/soc/system_stm32h7xx.c)

target_sources(${MODULE_NAME} PRIVATE ${STM32_HAL_LIB_DIR}/drivers/src/stm32h7xx_ll_utils.c)

