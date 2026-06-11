cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED p)
    message(FATAL_ERROR "You must define product. Example: -Dp=demo")
endif()

if(DEFINED t)
    set(TOOLCHAIN ${t})
else()
    set(TOOLCHAIN "armgcc9")
    message(STATUS "Toolchain not specified, using default: ${TOOLCHAIN}")
endif()

if(DEFINED f AND NOT DEFINED FIRMWARE_TYPE)
    set(FIRMWARE_TYPE ${f})
endif()
if(NOT DEFINED FIRMWARE_TYPE)
    set(FIRMWARE_TYPE app)
endif()
string(TOLOWER "${FIRMWARE_TYPE}" FIRMWARE_TYPE)
set(_valid_firmware_types app preloader loader upgrade)
if(NOT FIRMWARE_TYPE IN_LIST _valid_firmware_types)
    message(FATAL_ERROR "Invalid FIRMWARE_TYPE='${FIRMWARE_TYPE}', expected app/preloader/loader/upgrade")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/toolchain/${TOOLCHAIN}.cmake)

set(PROJECT_NAME ${p})
set(PRODUCT_NAME ${p})
if(FIRMWARE_TYPE STREQUAL "app")
    set(FIRMWARE_OUTPUT_NAME ${PROJECT_NAME})
else()
    set(FIRMWARE_OUTPUT_NAME ${PROJECT_NAME}_${FIRMWARE_TYPE})
endif()

project(${PROJECT_NAME} LANGUAGES C CXX ASM)
message(STATUS "Product: ${PRODUCT_NAME}")
message(STATUS "Firmware type: ${FIRMWARE_TYPE}")
message(STATUS "Firmware output: ${FIRMWARE_OUTPUT_NAME}")

include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/env.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/modules/dts.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/modules/kconfig.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/toolchain/compiler.cmake)

include_directories(${BINARY_DIR_INCLUDE_GENERATED})

add_subdirectory(embedded)
add_subdirectory(component)
add_subdirectory(bootloader/common)
add_subdirectory(${PROJECT_DIR})

add_executable(${PROJECT_NAME} ${NULL_C})
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${FIRMWARE_OUTPUT_NAME})
target_link_libraries(${PROJECT_NAME}
    embedded
    app
)

set_source_files_properties(${NULL_C} OBJECT_DEPENDS ${LINKER_SCRIPT})

add_custom_command(
  TARGET "${PROJECT_NAME}"
  POST_BUILD
  COMMAND ${PYTHON_EXECUTABLE} ${TOOLS_DIR}/scripts/gcc_compress.py
          -f "$<TARGET_FILE:${PROJECT_NAME}>"
  COMMAND ${CMAKE_OBJCOPY} -Obinary "$<TARGET_FILE:${PROJECT_NAME}>" "${FIRMWARE_OUTPUT_NAME}.bin"
  COMMAND ${CMAKE_OBJCOPY} -Oihex "$<TARGET_FILE:${PROJECT_NAME}>" "${FIRMWARE_OUTPUT_NAME}.hex"
  COMMENT "Compressing RW data and building ${FIRMWARE_OUTPUT_NAME}.bin/.hex"
  COMMAND ${CMAKE_SIZE} --format=berkeley "$<TARGET_FILE:${PROJECT_NAME}>"
)
