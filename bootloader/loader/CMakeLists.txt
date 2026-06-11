cmake_minimum_required(VERSION 3.20)

set(MODULE_NAME app)
add_library(${MODULE_NAME} ${NULL_C})

set(BOOT_FLASH_MAP_SOURCE ${PROJECT_BOOT_COMMON_DIR}/flash_map.cc)
if(NOT EXISTS "${BOOT_FLASH_MAP_SOURCE}")
    message(FATAL_ERROR "Missing product flash map: ${BOOT_FLASH_MAP_SOURCE}")
endif()

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/soc/gd/gd32f50x/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
        ${BSP_DIR}/soc/gd/gd32f50x
        ${TOP_DIR}/component/link/include
        ${PROJECT_PRODUCT_DIR}/include
)

target_sources(${MODULE_NAME}
    PRIVATE
        src/image_check.cc
        src/boot_logic.cc
        src/jump_app.cc
        src/boot_main.cc
        src/boot_init.cc
        upgrade_logic/upgrade_state.cc
        upgrade_logic/flash_copy.cc
        protocol/protocol.cc
        ${TOP_DIR}/component/link/src/codec.cc
        ${TOP_DIR}/component/link/src/crc16.cc
        transport.cc
        ${DRIVERS_GENERATED_CC}
        ${PINCTRL_GENERATED_CC}
        ${BOOT_FLASH_MAP_SOURCE}
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        boot_common
        drivers
        osal
)
