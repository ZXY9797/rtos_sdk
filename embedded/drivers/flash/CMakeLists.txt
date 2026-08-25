set(MODULE_NAME flash)
add_library(${MODULE_NAME} STATIC)

target_sources_ifdef(CONFIG_FLASH_STM32 ${MODULE_NAME} PRIVATE flash_stm32.cc)
target_sources_ifdef(CONFIG_FLASH_GD32 ${MODULE_NAME} PRIVATE flash_gd32.cc)
target_sources_ifdef(CONFIG_FLASH_GOODIX ${MODULE_NAME} PRIVATE flash_goodix.cc)
if(CONFIG_FLASH_GOODIX)
    target_compile_options(${MODULE_NAME}
        PRIVATE
            -mlong-calls
            -Wno-strict-aliasing
            -Wno-error=strict-aliasing
    )

endif()

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
)
target_include_directories_ifdef(CONFIG_FLASH_GOODIX ${MODULE_NAME} PRIVATE
    ${TOP_DIR}/component/ble/goodix/sdk
    ${BSP_DIR}/soc/goodix/gr5525x/include
    ${BSP_DIR}/soc/goodix/gr5525x/include/hal
    ${BSP_DIR}/soc/goodix/gr5525x/include/platform)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        soc
)
