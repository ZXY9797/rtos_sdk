target_include_directories(soc
    PUBLIC
        ${PROJECT_CONFIG_DIR}
)

# The Goodix SDK headers intentionally use typed half-word MMIO accessors on
# word-sized register definitions. Treat only the vendor boundary as system
# headers so those implementation-specific diagnostics do not hide warnings
# from SDK-owned code.
target_include_directories(soc SYSTEM
    PUBLIC
        .
        include
        include/hal
        include/app
        include/platform
        include/cmsis
    PRIVATE
        ${BSP_DIR}/include
        ${TOP_DIR}/component/ble/goodix/libraries/ring_buffer
        ${TOP_DIR}/component/ble/goodix/libraries/app_assert
        ${TOP_DIR}/component/ble/goodix/libraries/app_timer
        ${TOP_DIR}/component/ble/goodix/sdk
)

set(SOC_SOURCES
    soc.c
    src/system_gr5525x.c
    src/startup_gr55xx.s
    src/platform/gr_interrupt.c
    src/platform/gr_platform.c
    src/platform/gr_system.c
    src/hal/app_uart.c
    src/hal/app_uart_dma.c
    src/hal/app_spi.c
    src/hal/app_spi_dma.c
    src/hal/app_i2c.c
    src/hal/app_i2c_dma.c
    src/hal/app_dma.c
    src/hal/app_gpiote.c
    src/hal/app_io.c
    src/hal/app_pwm.c
    src/hal/app_pwm_dma.c
    src/hal/app_adc.c
    src/hal/app_adc_dma.c
    src/hal/app_rtc.c
    src/hal/app_tim.c
    src/hal/app_dual_tim.c
    src/hal/app_qspi.c
    src/hal/app_qspi_dma.c
    src/hal/app_dspi.c
    src/hal/app_dspi_dma.c
    src/hal/app_i2s.c
    src/hal/app_i2s_dma.c
    src/hal/app_pdm.c
    src/hal/app_rng.c
    src/hal/app_aon_wdt.c
    src/hal/app_bod.c
    src/hal/app_comp.c
    src/hal/app_pwr_mgmt.c
    src/hal/app_soft_encoder.c
    src/hal/gr55xx_delay.c
    src/hal/app_rtos_cfg.c
    src/hal/app_graphics_qspi.c
)

target_sources(soc
    PRIVATE
        ${SOC_SOURCES}
)

target_compile_definitions(soc
    PUBLIC
        GR5525X
        USE_HAL_DRIVER
        CFG_LAYER_TAG_SDK
)

# GR5525 BLE and peripheral libraries call services implemented in Stack ROM.
# Bind every Goodix image to the exact ROM ABI exported by the matching SDK;
# never allow those symbols to remain unresolved in the output ELF.
set(GOODIX_ROM_SYMBOL_FILE
    ${CMAKE_CURRENT_SOURCE_DIR}/linker/rom_symbol_gcc_v1.0.3_patch_2.txt)
set(GOODIX_ROM_SYMBOL_SHA256
    bd3721bdcf60399431b149d946e76aa65b1549b5ed21d14344c7892c0e0218aa)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    ${GOODIX_ROM_SYMBOL_FILE})
file(SHA256 ${GOODIX_ROM_SYMBOL_FILE} _goodix_rom_symbol_sha256)
if(NOT _goodix_rom_symbol_sha256 STREQUAL GOODIX_ROM_SYMBOL_SHA256)
    message(FATAL_ERROR
        "GR5525 ROM symbol table does not match Goodix SDK v1.0.3_patch_2: "
        "expected ${GOODIX_ROM_SYMBOL_SHA256}, got ${_goodix_rom_symbol_sha256}")
endif()
target_link_options(soc INTERFACE ${GOODIX_ROM_SYMBOL_FILE})
set_property(TARGET soc APPEND PROPERTY INTERFACE_LINK_DEPENDS
    ${GOODIX_ROM_SYMBOL_FILE})

# Goodix SDK sources contain deprecated 'register' and sign-compare warnings.
target_compile_options(soc
    PRIVATE
        -Wno-sign-compare
        -Wno-error=sign-compare
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-register>
        $<$<COMPILE_LANGUAGE:CXX>:-Wno-error=register>
)

target_link_libraries(soc
    PUBLIC
        cmsis
)
