set(ARCH_V2_NAME_LIST arm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TOP_DIR ${SDK_ROOT})
set(TOOLS_DIR ${TOP_DIR}/tools)
set(BSP_DIR ${TOP_DIR}/embedded)
set(APP_DIR ${TOP_DIR}/app)
set(BOOTLOADER_DIR ${TOP_DIR}/bootloader)

# Product-wide defaults are shared by App and every Boot image.
include("${TOP_DIR}/products/${PROJECT_NAME}/product.cmake" OPTIONAL)

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
    set(PROJECT_BOOT_PRODUCT_DIR "${BOOTLOADER_DIR}/product/${PROJECT_NAME}")
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

# A configuration-only override still uses the product's physical board.
# Keep explicit legacy CONFIG_DIR/board.dts projects usable during migration.
if(NOT DEFINED BOARD AND DEFINED SDK_DEFAULT_BOARD
   AND NOT (DEFINED CONFIG_DIR AND EXISTS "${PROJECT_CONFIG_DIR}/board.dts"))
    set(BOARD "${SDK_DEFAULT_BOARD}")
endif()
if(NOT DEFINED PROFILE)
    set(PROFILE default)
endif()
if(NOT PROFILE MATCHES "^[A-Za-z0-9_-]+$")
    message(FATAL_ERROR "Invalid PROFILE: ${PROFILE}")
endif()

if(DEFINED BOARD)
    foreach(_root IN ITEMS ${BOARD_ROOT} "${TOP_DIR}/boards")
        if(EXISTS "${_root}/${BOARD}/board.dts")
            get_filename_component(BOARD_DIR "${_root}/${BOARD}" ABSOLUTE)
            break()
        endif()
    endforeach()
    if(NOT DEFINED BOARD_DIR)
        message(FATAL_ERROR "Board ${BOARD} not found in BOARD_ROOT or SDK boards")
    endif()
endif()

if(DEFINED BOARD_DIR)
    set(SDK_BOARD_DTS "${BOARD_DIR}/board.dts")
else()
    set(SDK_BOARD_DTS "${PROJECT_CONFIG_DIR}/board.dts")
endif()
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
