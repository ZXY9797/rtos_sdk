set(MODULE_NAME flash)
add_library(${MODULE_NAME} STATIC)

target_sources_ifdef(CONFIG_FLASH_STM32 ${MODULE_NAME} PRIVATE flash_stm32.cc)
target_sources_ifdef(CONFIG_FLASH_GD32 ${MODULE_NAME} PRIVATE flash_gd32.cc)

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
