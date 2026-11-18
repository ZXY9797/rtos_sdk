set(ARCH_V2_NAME_LIST arm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TOP_DIR ${SDK_ROOT})
set(TOOLS_DIR ${TOP_DIR}/tools)
set(BSP_DIR ${TOP_DIR}/embedded)
set(APP_DIR ${TOP_DIR}/app)
set(BOOTLOADER_DIR ${TOP_DIR}/bootloader)

# Hardware belongs to the product; Boot images reuse the same description.
set(PROJECT_HARDWARE_DIR "${APP_DIR}/product/${PROJECT_NAME}/config")
if(DEFINED BOARD OR DEFINED BOARD_ROOT)
    message(FATAL_ERROR
        "Select the product with -Dp, not BOARD/BOARD_ROOT. "
        "Clear old board cache entries with -UBOARD -UBOARD_ROOT.")
endif()

if(NOT DEFINED FIRMWARE_TYPE)
    set(FIRMWARE_TYPE app)
endif()

if(FIRMWARE_TYPE STREQUAL "app")
    if(DEFINED APPLICATION)
        get_filename_component(PROJECT_DIR "${APPLICATION}" ABSOLUTE)
    else()
        set(PROJECT_DIR "${APP_DIR}/product/${PROJECT_NAME}")
    endif()
    if(NOT EXISTS "${PROJECT_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Product app '${PROJECT_NAME}' not found")
    endif()
else()
    set(PROJECT_DIR "${BOOTLOADER_DIR}/${FIRMWARE_TYPE}")
    set(PROJECT_BOOT_PRODUCT_DIR "${APP_DIR}/product/${PROJECT_NAME}/boot")
    set(PROJECT_BOOT_COMMON_DIR "${PROJECT_BOOT_PRODUCT_DIR}/common")
    include("${PROJECT_BOOT_PRODUCT_DIR}/product.cmake" OPTIONAL)
    set(PROJECT_PRODUCT_DIR "${PROJECT_BOOT_PRODUCT_DIR}/${FIRMWARE_TYPE}")

    if(NOT EXISTS "${PROJECT_DIR}/CMakeLists.txt")
        message(FATAL_ERROR "Boot firmware type '${FIRMWARE_TYPE}' not found")
    endif()
    if(NOT EXISTS "${PROJECT_PRODUCT_DIR}")
        message(FATAL_ERROR
            "Product boot config not found: ${PROJECT_PRODUCT_DIR}")
    endif()
endif()

set(APPLICATION_SOURCE_DIR "${PROJECT_DIR}")
include("${PROJECT_DIR}/product.cmake" OPTIONAL)
if(DEFINED CONFIG_DIR)
    get_filename_component(
        PROJECT_CONFIG_DIR "${CONFIG_DIR}" ABSOLUTE
        BASE_DIR "${CMAKE_SOURCE_DIR}")
elseif(FIRMWARE_TYPE STREQUAL "app")
    set(PROJECT_CONFIG_DIR "${PROJECT_DIR}/config")
else()
    set(PROJECT_CONFIG_DIR "${PROJECT_PRODUCT_DIR}/config")
endif()

# Configuration-only overrides keep the product hardware. An independent App
# or an explicit image config can provide its own complete hardware description.
if(FIRMWARE_TYPE STREQUAL "app" AND EXISTS "${PROJECT_DIR}/config/board.dts")
    set(PROJECT_HARDWARE_DIR "${PROJECT_DIR}/config")
endif()
if(EXISTS "${PROJECT_CONFIG_DIR}/board.dts")
    set(PROJECT_HARDWARE_DIR "${PROJECT_CONFIG_DIR}")
endif()
if(NOT DEFINED PROFILE)
    set(PROFILE default)
endif()
if(NOT PROFILE MATCHES "^[A-Za-z0-9_-]+$")
    message(FATAL_ERROR "Invalid PROFILE: ${PROFILE}")
endif()

set(SDK_BOARD_DTS "${PROJECT_HARDWARE_DIR}/board.dts")
if(NOT EXISTS "${SDK_BOARD_DTS}")
    message(FATAL_ERROR "Missing board DTS: ${SDK_BOARD_DTS}")
endif()

include("${TOOLS_DIR}/cmake/modules/image_config.cmake")
sdk_select_image_configuration()

if(FIRMWARE_TYPE STREQUAL "app" AND EXISTS "${PROJECT_DIR}/linker.ld")
    set(PROJECT_LINKER_SCRIPT "${PROJECT_DIR}/linker.ld")
elseif(NOT FIRMWARE_TYPE STREQUAL "app"
       AND EXISTS "${PROJECT_PRODUCT_DIR}/linker.ld")
    set(PROJECT_LINKER_SCRIPT "${PROJECT_PRODUCT_DIR}/linker.ld")
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
