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
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/modules/boot_layout.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/modules/dts.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/modules/kconfig.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/toolchain/compiler.cmake)

add_subdirectory(embedded)
add_subdirectory(component)
add_subdirectory(bootloader/common)
add_subdirectory(${PROJECT_DIR})

if(TARGET device_codegen)
    add_dependencies(app device_codegen)
    add_dependencies(drivers device_codegen)
endif()

add_executable(${PROJECT_NAME}
    ${NULL_C}
    ${BOOT_FLASH_MAP_SOURCE}
    $<TARGET_OBJECTS:arch>
    $<TARGET_OBJECTS:system_fault>
)
if(FIRMWARE_TYPE STREQUAL "app")
    target_sources(${PROJECT_NAME} PRIVATE $<TARGET_OBJECTS:system>)
endif()
set_target_properties(${PROJECT_NAME} PROPERTIES OUTPUT_NAME ${FIRMWARE_OUTPUT_NAME})
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${BOOTLOADER_DIR}/include
)
target_link_options(${PROJECT_NAME} PRIVATE
    ${SDK_EXECUTABLE_LINK_OPTIONS}
    ${BOOT_LAYOUT_LINK_OPTIONS})
target_link_libraries(${PROJECT_NAME}
    PRIVATE
    embedded
    app
)

# Apply the toolchain/config interface to each concrete build target. This
# keeps generated headers and compiler policy target-scoped instead of leaking
# directory-global include paths and flags into unrelated subdirectories.
function(sdk_collect_build_targets directory output)
    get_property(_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    get_property(_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_subdirectory IN LISTS _subdirectories)
        sdk_collect_build_targets("${_subdirectory}" _child_targets)
        list(APPEND _targets ${_child_targets})
    endforeach()
    set(${output} "${_targets}" PARENT_SCOPE)
endfunction()

sdk_collect_build_targets("${CMAKE_SOURCE_DIR}" _sdk_targets)
foreach(_target IN LISTS _sdk_targets)
    if(_target STREQUAL sdk_build_config)
        continue()
    endif()
    get_target_property(_imported ${_target} IMPORTED)
    get_target_property(_type ${_target} TYPE)
    if(_imported)
        continue()
    elseif(_type STREQUAL "INTERFACE_LIBRARY")
        target_link_libraries(${_target} INTERFACE sdk_build_config)
    elseif(_type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
        target_link_libraries(${_target} PRIVATE sdk_build_config)
    endif()
endforeach()

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

if(FIRMWARE_TYPE STREQUAL "loader")
    add_custom_command(
      TARGET "${PROJECT_NAME}"
      POST_BUILD
      COMMAND ${PYTHON_EXECUTABLE}
              ${BOOTLOADER_DIR}/scripts/check_image_size.py
              --image "${FIRMWARE_OUTPUT_NAME}.bin"
              --max-size ${BOOT_LOADER_SIZE}
              --label "${PROJECT_NAME} loader"
      COMMENT "Checking loader binary against its physical partition"
    )
endif()
