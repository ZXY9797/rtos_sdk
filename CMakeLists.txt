CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

if(NOT DEFINED p)
    message(FATAL_ERROR "You must defined Project. eg.-Dp=xxx")
endif()

if(DEFINED t)
    set(TOOLCHAIN ${t})
else()
    set(TOOLCHAIN "armgcc")
    message(STATUS "Toolchain not specified, using default: ${TOOLCHAIN}")
endif()

# 工具链
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/toolchain/${TOOLCHAIN}.cmake)

set(PROJECT_NAME        ${p})

project(${PROJECT_NAME} LANGUAGES C ASM)

include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/env.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/modules/dts.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/modules/kconfig.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/toolchain/compiler.cmake)

include_directories(${BINARY_DIR_INCLUDE_GENERATED})


add_subdirectory(embedded)
add_subdirectory(${PROJECT_DIR})

add_executable(${PROJECT_NAME} ${NULL_C})
target_link_libraries(${PROJECT_NAME} app)

set_source_files_properties(${NULL_C} OBJECT_DEPENDS ${LINKER_SCRIPT})

add_custom_command(
  TARGET "${PROJECT_NAME}"
  POST_BUILD
  # Build .hex and .bin files
  COMMAND ${CMAKE_OBJCOPY} -Obinary "${PROJECT_NAME}${CMAKE_OUTPUT_SUFFIX}" "${PROJECT_NAME}.bin"
  COMMAND ${CMAKE_OBJCOPY} -Oihex "${PROJECT_NAME}${CMAKE_OUTPUT_SUFFIX}" "${PROJECT_NAME}.hex"
  COMMENT "Building ${PROJECT_NAME}.bin and ${PROJECT_NAME}.hex"
  # Display sizes
  COMMAND ${CMAKE_SIZE} --format=berkeley ${PROJECT_NAME}${CMAKE_OUTPUT_SUFFIX}
)