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

    # GR5525 SDK keeps exflash HAL implementations in ROM. These addresses
    # match Goodix GR5525.SDK platform/soc/linker/gcc/rom_symbol_gcc.txt.
    target_link_options(${MODULE_NAME}
        INTERFACE
            -Wl,--defsym,hal_exflash_init=0x0001983d
            -Wl,--defsym,hal_exflash_read=0x00019a05
            -Wl,--defsym,hal_exflash_write=0x00019d15
            -Wl,--defsym,hal_exflash_erase=0x0001969d
    )
endif()

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
    PRIVATE
        ${TOP_DIR}/component/ble/goodix/sdk
        ${BSP_DIR}/soc/goodix/gr5525x/include
        ${BSP_DIR}/soc/goodix/gr5525x/include/hal
        ${BSP_DIR}/soc/goodix/gr5525x/include/platform
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        soc
)
