set(ARCH_V2_NAME_LIST arm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TOP_DIR ${CMAKE_SOURCE_DIR})
set(TOOLS_DIR ${TOP_DIR}/tools)
set(BSP_DIR ${TOP_DIR}/embedded)
set(APP_DIR ${TOP_DIR}/app)
set(BOOTLOADER_DIR ${TOP_DIR}/bootloader)

if(NOT DEFINED FIRMWARE_TYPE)
    set(FIRMWARE_TYPE app)
endif()

if(FIRMWARE_TYPE STREQUAL "app")
    if(EXISTS "${APP_DIR}/product/${PROJECT_NAME}")
        set(PROJECT_DIR "${APP_DIR}/product/${PROJECT_NAME}")
    elseif(EXISTS "${APP_DIR}/${PROJECT_NAME}")
        set(PROJECT_DIR "${APP_DIR}/${PROJECT_NAME}")
    elseif(EXISTS "${TOP_DIR}/${PROJECT_NAME}")
        set(PROJECT_DIR "${TOP_DIR}/${PROJECT_NAME}")
    else()
        message(FATAL_ERROR "Product app '${PROJECT_NAME}' not found")
    endif()
else()
    set(PROJECT_DIR "${BOOTLOADER_DIR}/${FIRMWARE_TYPE}")
    set(PROJECT_BOOT_PRODUCT_DIR "${BOOTLOADER_DIR}/product/${PROJECT_NAME}")
    set(PROJECT_BOOT_COMMON_DIR "${PROJECT_BOOT_PRODUCT_DIR}/common")
    set(PROJECT_PRODUCT_DIR "${PROJECT_BOOT_PRODUCT_DIR}/${FIRMWARE_TYPE}")

    if(NOT EXISTS "${PROJECT_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Boot firmware type '${FIRMWARE_TYPE}' not found")
    endif()
    if(NOT EXISTS "${PROJECT_PRODUCT_DIR}")
        message(FATAL_ERROR "Product boot config not found: ${PROJECT_PRODUCT_DIR}")
    endif()
endif()

if(DEFINED CONFIG_DIR)
    get_filename_component(PROJECT_CONFIG_DIR "${CONFIG_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
elseif(FIRMWARE_TYPE STREQUAL "app")
    set(PROJECT_CONFIG_DIR "${PROJECT_DIR}/config")
else()
    set(PROJECT_CONFIG_DIR "${PROJECT_PRODUCT_DIR}/config")
endif()

if(NOT EXISTS "${PROJECT_CONFIG_DIR}/board.dts")
    message(FATAL_ERROR "Missing board.dts in ${PROJECT_CONFIG_DIR}")
endif()

if(NOT DEFINED LINKER_SCRIPT)
    if(FIRMWARE_TYPE STREQUAL "app" AND EXISTS "${PROJECT_DIR}/linker.ld")
        set(PROJECT_LINKER_SCRIPT "${PROJECT_DIR}/linker.ld")
    elseif(NOT FIRMWARE_TYPE STREQUAL "app" AND EXISTS "${PROJECT_PRODUCT_DIR}/linker.ld")
        set(PROJECT_LINKER_SCRIPT "${PROJECT_PRODUCT_DIR}/linker.ld")
    endif()
endif()

set(NULL_C ${TOOLS_DIR}/cmake/null.c)

set(BINARY_DIR_INCLUDE ${PROJECT_BINARY_DIR}/include)
set(BINARY_DIR_INCLUDE_GENERATED ${BINARY_DIR_INCLUDE}/generated)
file(MAKE_DIRECTORY ${BINARY_DIR_INCLUDE_GENERATED})

set(KCONFIG_BINARY_DIR ${PROJECT_BINARY_DIR}/Kconfig)
file(MAKE_DIRECTORY ${KCONFIG_BINARY_DIR})

set(CMAKE_MODULE_PATH
    ${CMAKE_MODULE_PATH}
    ${TOOLS_DIR}/cmake
    ${TOOLS_DIR}/cmake/modules
    ${TOOLS_DIR}/cmake/toolchain
    ${TOOLS_DIR}/cmake/toolchain/core
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
