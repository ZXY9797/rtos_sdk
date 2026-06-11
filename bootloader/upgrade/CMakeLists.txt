cmake_minimum_required(VERSION 3.20)

set(MODULE_NAME app)
add_library(${MODULE_NAME} ${NULL_C})

set(UPGRADE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)
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
        ${UPGRADE_INCLUDE_DIR}
        ${PROJECT_PRODUCT_DIR}/include
)

target_sources(${MODULE_NAME}
    PRIVATE
        src/upgrade_main.cc
        src/loader_flash.cc
        ${BOOT_FLASH_MAP_SOURCE}
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        boot_common
)
