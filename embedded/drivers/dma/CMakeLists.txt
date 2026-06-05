set(MODULE_NAME dma)

add_library(${MODULE_NAME} ${NULL_C})

target_sources_ifdef(CONFIG_DMA_GD32 ${MODULE_NAME}
    PRIVATE
        dma_gd32.cc
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
    PRIVATE
        ${BSP_DIR}/soc/gd/gd32f50x/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        soc
)
