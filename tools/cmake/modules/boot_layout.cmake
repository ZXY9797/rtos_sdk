include_guard(GLOBAL)

find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

set(BOOT_LAYOUT_MANIFEST
    "${BOOTLOADER_DIR}/product/${PROJECT_NAME}/layout.json")
set(BOOT_LAYOUT_SCRIPT "${BOOTLOADER_DIR}/scripts/boot_layout.py")
if(NOT EXISTS "${BOOT_LAYOUT_MANIFEST}")
    message(FATAL_ERROR "Missing boot layout manifest: ${BOOT_LAYOUT_MANIFEST}")
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${BOOT_LAYOUT_MANIFEST}"
    "${BOOT_LAYOUT_SCRIPT}")

set(_boot_layout_values
    "${BINARY_DIR_INCLUDE_GENERATED}/boot_layout_values.cmake")
execute_process(
    COMMAND ${Python3_EXECUTABLE} "${BOOT_LAYOUT_SCRIPT}" "${PROJECT_NAME}"
            --cmake-output "${_boot_layout_values}"
    RESULT_VARIABLE _boot_layout_result
    ERROR_VARIABLE _boot_layout_error
)
if(NOT _boot_layout_result EQUAL 0)
    message(FATAL_ERROR
        "Boot layout validation failed for ${BOOT_LAYOUT_MANIFEST}:\n"
        "${_boot_layout_error}")
endif()
include("${_boot_layout_values}")

foreach(_partition PRELOADER LOADER SLOT0 UPGRADE STORAGE BOOT_CTRL)
    math(EXPR BOOT_${_partition}_ORIGIN
         "${BOOT_FLASH_BASE} + ${BOOT_${_partition}_OFFSET}"
         OUTPUT_FORMAT HEXADECIMAL)
    string(REGEX REPLACE "^0[xX]" ""
           BOOT_${_partition}_UNIT_ADDRESS
           "${BOOT_${_partition}_OFFSET}")
endforeach()
string(REGEX REPLACE "^0[xX]" "" BOOT_FLASH_UNIT_ADDRESS
       "${BOOT_FLASH_BASE}")

math(EXPR BOOT_SLOT0_PAYLOAD_ORIGIN
     "${BOOT_SLOT0_ORIGIN} + ${BOOT_IMAGE_HEADER_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR BOOT_SLOT0_PAYLOAD_SIZE
     "${BOOT_SLOT0_SIZE} - ${BOOT_IMAGE_HEADER_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR BOOT_PRODUCT_INFO_ORIGIN
     "${BOOT_SLOT0_ORIGIN} + ${BOOT_PRODUCT_INFO_OFFSET}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR BOOT_APP_LINK_SIZE
     "${BOOT_SLOT0_PAYLOAD_SIZE} + ${BOOT_RAM_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)
math(EXPR BOOT_RAM_END
     "${BOOT_RAM_BASE} + ${BOOT_RAM_SIZE}"
     OUTPUT_FORMAT HEXADECIMAL)

set(BOOT_FLASH_MAP_SOURCE
    "${BINARY_DIR_INCLUDE_GENERATED}/boot_flash_map.cc")
configure_file(
    "${BOOTLOADER_DIR}/common/flash_map.cc.in"
    "${BOOT_FLASH_MAP_SOURCE}"
    @ONLY)
configure_file(
    "${BOOTLOADER_DIR}/include/boot/layout.h.in"
    "${BINARY_DIR_INCLUDE_GENERATED}/boot_layout.h"
    @ONLY)
set(BOOT_PARTITIONS_DTS
    "${BINARY_DIR_INCLUDE_GENERATED}/boot_partitions.dts")
configure_file(
    "${BOOTLOADER_DIR}/common/boot_partitions.dts.in"
    "${BOOT_PARTITIONS_DTS}"
    @ONLY)

set(BOOT_LAYOUT_LINK_OPTIONS)
foreach(_symbol
        BOOT_FLASH_BASE
        BOOT_FLASH_SIZE BOOT_RAM_BASE BOOT_RAM_SIZE
        BOOT_IMAGE_HEADER_SIZE BOOT_PRODUCT_INFO_OFFSET BOOT_PRODUCT_ID
        BOOT_PRELOADER_ORIGIN BOOT_PRELOADER_SIZE
        BOOT_LOADER_ORIGIN BOOT_LOADER_SIZE
        BOOT_SLOT0_ORIGIN BOOT_SLOT0_SIZE
        BOOT_SLOT0_PAYLOAD_ORIGIN BOOT_SLOT0_PAYLOAD_SIZE
        BOOT_PRODUCT_INFO_ORIGIN BOOT_APP_LINK_SIZE
        BOOT_UPGRADE_ORIGIN BOOT_UPGRADE_SIZE
        BOOT_STORAGE_ORIGIN BOOT_STORAGE_SIZE
        BOOT_BOOT_CTRL_ORIGIN BOOT_BOOT_CTRL_SIZE)
    list(APPEND BOOT_LAYOUT_LINK_OPTIONS
         "-Wl,--defsym=${_symbol}=${${_symbol}}")
endforeach()
list(APPEND BOOT_LAYOUT_LINK_OPTIONS
     "-Wl,--defsym=__ram_region_start=${BOOT_RAM_BASE}"
     "-Wl,--defsym=__ram_region_end=${BOOT_RAM_END}")

message(STATUS "Boot layout: ${BOOT_LAYOUT_MANIFEST}")
