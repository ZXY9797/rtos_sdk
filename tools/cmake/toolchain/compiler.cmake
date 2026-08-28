include(extensions)

dt_arch_type(arch_core 0)
include(${arch_core})

add_library(sdk_build_config INTERFACE)
target_include_directories(sdk_build_config INTERFACE
    ${BINARY_DIR_INCLUDE_GENERATED})

separate_arguments(_sdk_cpu_flags UNIX_COMMAND "${MCPU_FLAGS}")
separate_arguments(_sdk_vfp_flags UNIX_COMMAND "${VFP_FLAGS}")
separate_arguments(_sdk_system_flags UNIX_COMMAND "${SYSTEM_PATH}")

set(_sdk_optimization -O2)
if(FIRMWARE_TYPE STREQUAL "loader")
    set(_sdk_optimization -Os)
endif()

set(_sdk_c_family_options
    ${_sdk_cpu_flags}
    ${_sdk_vfp_flags}
    ${_sdk_system_flags}
    --specs=nano.specs
    --specs=nosys.specs
    ${_sdk_optimization}
    -g
    -Werror
    -Wall
    -fmessage-length=0
    -ffunction-sections
    -fdata-sections
    -imacros
    ${AUTOCONF_H})
foreach(_option IN LISTS _sdk_c_family_options)
    target_compile_options(sdk_build_config INTERFACE
        "$<$<OR:$<COMPILE_LANGUAGE:C>,$<COMPILE_LANGUAGE:CXX>>:${_option}>")
endforeach()

if(ARMGCC9_CXX2A)
    set(_sdk_cxx_standard -std=c++2a)
else()
    set(_sdk_cxx_standard -std=c++20)
endif()
target_compile_options(sdk_build_config INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
    $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
    $<$<COMPILE_LANGUAGE:CXX>:${_sdk_cxx_standard}>)

if(FIRMWARE_TYPE STREQUAL "loader")
    target_compile_options(sdk_build_config INTERFACE
        $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>)
endif()

set(_sdk_asm_options
    ${_sdk_cpu_flags}
    ${_sdk_vfp_flags}
    -x
    assembler-with-cpp
    -imacros
    ${AUTOCONF_H}
    -D_ASMLANGUAGE)
foreach(_option IN LISTS _sdk_asm_options)
    target_compile_options(sdk_build_config INTERFACE
        "$<$<COMPILE_LANGUAGE:ASM>:${_option}>")
endforeach()

target_link_options(sdk_build_config INTERFACE
    ${_sdk_cpu_flags}
    ${_sdk_vfp_flags}
    ${_sdk_system_flags}
    --specs=nano.specs
    --specs=nosys.specs)

dt_get_soc_name(soc_name)
set(DEFAULT_LINKER_SCRIPT ${BSP_DIR}/linkscript/${soc_name}.ld)
if(DEFINED PROJECT_LINKER_SCRIPT)
    set(DEFAULT_LINKER_SCRIPT ${PROJECT_LINKER_SCRIPT})
endif()
if(DEFINED LINKER_SCRIPT)
    if(DEFINED SDK_LAST_DEFAULT_LINKER_SCRIPT
       AND LINKER_SCRIPT STREQUAL SDK_LAST_DEFAULT_LINKER_SCRIPT)
        set(LINKER_SCRIPT ${DEFAULT_LINKER_SCRIPT})
    endif()
    get_filename_component(
        LINKER_SCRIPT "${LINKER_SCRIPT}" ABSOLUTE
        BASE_DIR "${CMAKE_SOURCE_DIR}")
else()
    set(LINKER_SCRIPT ${DEFAULT_LINKER_SCRIPT})
endif()
if(NOT EXISTS "${LINKER_SCRIPT}")
    message(FATAL_ERROR "Linker script not found: ${LINKER_SCRIPT}")
endif()
set(LINKER_SCRIPT ${LINKER_SCRIPT} CACHE PATH "Linker script path" FORCE)
get_filename_component(
    _sdk_default_linker_script "${DEFAULT_LINKER_SCRIPT}" ABSOLUTE
    BASE_DIR "${CMAKE_SOURCE_DIR}")
set(SDK_LAST_DEFAULT_LINKER_SCRIPT
    "${_sdk_default_linker_script}" CACHE INTERNAL
    "Last automatically selected SDK linker script" FORCE)
message(STATUS "Link script: ${LINKER_SCRIPT}")

if(DEFINED FIRMWARE_OUTPUT_NAME)
    set(LINK_MAP_NAME ${FIRMWARE_OUTPUT_NAME})
else()
    set(LINK_MAP_NAME ${PROJECT_NAME})
endif()

set(SDK_EXECUTABLE_LINK_OPTIONS
    "-T${LINKER_SCRIPT}"
    "-Wl,-Map=${PROJECT_BINARY_DIR}/${LINK_MAP_NAME}.map"
    "-Wl,--gc-sections,--print-memory-usage"
    "-Wl,-u,Default_Handler,-u,z_cstart")

if(FIRMWARE_TYPE STREQUAL "loader")
    list(APPEND SDK_EXECUTABLE_LINK_OPTIONS
        -nostartfiles
        "-Wl,--defsym=BOOT_STACK_SIZE=${CONFIG_BOOT_STACK_SIZE}")
endif()

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
