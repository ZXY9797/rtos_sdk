# Convert the Zephyr lowercase SoC name to the value expected by STM32Cube
# Example: "stm32f030xc" --TOUPPER--> "STM32F030XC" --REPLACE--> "STM32F030xC"
string(TOUPPER ${CONFIG_SOC} _STM32CUBE_CPU)
string(REPLACE "X" "x" STM32CUBE_CPU ${_STM32CUBE_CPU})

target_compile_definitions(soc
    PUBLIC
        ${STM32CUBE_CPU}
        USE_HAL_DRIVER
        USE_FULL_LL_DRIVER
)

if(CONFIG_CPU_CORTEX_M4)
  target_compile_definitions(soc PUBLIC CORE_CM4 CPU_CORTEX_M4)
elseif(CONFIG_CPU_CORTEX_M7)
  target_compile_definitions(soc PUBLIC CORE_CM7 CPU_CORTEX_M7)
elseif(CONFIG_CPU_CORTEX_M55)
  target_compile_definitions(soc PUBLIC CORE_CM55 CPU_CORTEX_M55)
elseif(CONFIG_CPU_CORTEX_A7)
  target_compile_definitions(soc PUBLIC CORE_CA7 CPU_CORTEX_A7)
elseif(CONFIG_CPU_CORTEX_M33)
  target_compile_definitions(soc PUBLIC CORE_CM33 CPU_CORTEX_M33)
endif()

if(CONFIG_CLOCK_STM32_HSE_CLOCK)
  target_compile_definitions(soc PUBLIC HSE_VALUE=${CONFIG_CLOCK_STM32_HSE_CLOCK})
endif()

target_include_directories(${MODULE_NAME}
  PUBLIC
    common
    include
  PRIVATE
    ${BSP_DIR}/arch
    ${BSP_DIR}/arch/common
)

target_link_libraries(${MODULE_NAME}
  PUBLIC
    cmsis
)

target_sources(${MODULE_NAME}
  PRIVATE
    common/stm32_backup_domain.c
)

add_subdirectory(${SOC_SERIES})
