set(MODULE_NAME uart)
add_library(${MODULE_NAME} STATIC)

target_sources_ifdef(CONFIG_UART_STM32 ${MODULE_NAME} PRIVATE uart_stm32.cc)
target_sources_ifdef(CONFIG_UART_GD32 ${MODULE_NAME} PRIVATE uart_gd32.cc)
target_sources_ifdef(CONFIG_UART_GR5525 ${MODULE_NAME} PRIVATE uart_gr5525.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
    PRIVATE
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/soc/gd/gd32f50x/include
        ${BSP_DIR}/soc/goodix/gr5525x/include
        ${BSP_DIR}/soc/goodix/gr5525x/include/hal
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        soc
        osal
)

if(CONFIG_DMA)
    target_link_libraries(${MODULE_NAME} PRIVATE dma)
endif()
