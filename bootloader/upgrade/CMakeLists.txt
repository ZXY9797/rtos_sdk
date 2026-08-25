cmake_minimum_required(VERSION 3.20)

set(MODULE_NAME app)
add_library(${MODULE_NAME} ${NULL_C})

set(UPGRADE_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)

if(CONFIG_BOOT_PRODUCTION)
    message(FATAL_ERROR
        "Production loader field upgrade is disabled until its container "
        "has an authenticated monotonic-version contract")
endif()

if(NOT DEFINED LOADER_PAYLOAD_FILE)
    message(FATAL_ERROR
        "Upgrade firmware requires -DLOADER_PAYLOAD_FILE=<built loader.bin>")
endif()
get_filename_component(LOADER_PAYLOAD_FILE "${LOADER_PAYLOAD_FILE}"
    ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
if(NOT EXISTS "${LOADER_PAYLOAD_FILE}")
    message(FATAL_ERROR "Loader payload not found: ${LOADER_PAYLOAD_FILE}")
endif()
set(LOADER_PAYLOAD_SOURCE
    ${CMAKE_CURRENT_BINARY_DIR}/loader_payload_generated.cc)
add_custom_command(
    OUTPUT ${LOADER_PAYLOAD_SOURCE}
    COMMAND ${PYTHON_EXECUTABLE}
            ${BOOTLOADER_DIR}/scripts/pack_loader_payload.py
            --input ${LOADER_PAYLOAD_FILE}
            --output ${LOADER_PAYLOAD_SOURCE}
    DEPENDS
        ${LOADER_PAYLOAD_FILE}
        ${BOOTLOADER_DIR}/scripts/pack_loader_payload.py
    COMMENT "Generating checked loader-upgrade payload"
    VERBATIM)
set_source_files_properties(${LOADER_PAYLOAD_SOURCE} PROPERTIES GENERATED TRUE)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
        ${UPGRADE_INCLUDE_DIR}
        ${PROJECT_PRODUCT_DIR}/include
)

target_sources(${MODULE_NAME}
    PRIVATE
        src/upgrade_init.cc
        src/upgrade_main.cc
        ${LOADER_PAYLOAD_SOURCE}
        ${PINCTRL_GENERATED_CC}
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        boot_common
        soc
)
