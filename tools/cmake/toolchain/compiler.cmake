include(extensions)

dt_arch_type(arch_core 0)
include(${arch_core})

set(CFCOMMON "${MCPU_FLAGS} ${VFP_FLAGS} ${SYSTEM_PATH} --specs=nano.specs -specs=rdimon.specs --specs=nosys.specs -Wall -fmessage-length=0 -ffunction-sections -fdata-sections")

set(CMAKE_C_FLAGS "-O2 -g -Werror ${CFCOMMON} -imacros ${AUTOCONF_H}")
if(ARMGCC9_CXX2A)
    set(CMAKE_CXX_FLAGS "-O2 -g -Werror ${CFCOMMON} -imacros ${AUTOCONF_H} -fno-exceptions -fno-rtti -std=c++2a")
else()
    set(CMAKE_CXX_FLAGS "-O2 -g -Werror ${CFCOMMON} -imacros ${AUTOCONF_H} -fno-exceptions -fno-rtti -std=c++20")
endif()
set(CMAKE_ASM_FLAGS "${MCPU_FLAGS} ${VFP_FLAGS} -x assembler-with-cpp -imacros ${AUTOCONF_H}")

add_compile_options($<$<COMPILE_LANGUAGE:ASM>:-D_ASMLANGUAGE>)

dt_get_soc_name(soc_name)
set(DEFAULT_LINKER_SCRIPT ${BSP_DIR}/linkscript/${soc_name}.ld)
if(DEFINED PROJECT_LINKER_SCRIPT)
    set(DEFAULT_LINKER_SCRIPT ${PROJECT_LINKER_SCRIPT})
endif()
if(DEFINED LINKER_SCRIPT)
    get_filename_component(LINKER_SCRIPT "${LINKER_SCRIPT}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
else()
    set(LINKER_SCRIPT ${DEFAULT_LINKER_SCRIPT})
endif()
if(NOT EXISTS "${LINKER_SCRIPT}")
    message(FATAL_ERROR "Linker script not found: ${LINKER_SCRIPT}")
endif()
set(LINKER_SCRIPT ${LINKER_SCRIPT} CACHE PATH "Linker script path" FORCE)
message(STATUS "Link script: ${LINKER_SCRIPT}")

set(VECTOR_TABLE_LINK_FLAG "-Wl,-u,Default_Handler,-u,main")

if(ARMGCC9_UNRESOLVED)
    set(UNRESOLVED_FLAG "-Wl,--unresolved-symbols=ignore-in-object-files -Wl,--allow-multiple-definition")
else()
    set(UNRESOLVED_FLAG "")
endif()

if(DEFINED FIRMWARE_OUTPUT_NAME)
    set(LINK_MAP_NAME ${FIRMWARE_OUTPUT_NAME})
else()
    set(LINK_MAP_NAME ${PROJECT_NAME})
endif()

set(CMAKE_EXE_LINKER_FLAGS
    "${CMAKE_EXE_LINKER_FLAGS} -T ${LINKER_SCRIPT} -Wl,-Map=${PROJECT_BINARY_DIR}/${LINK_MAP_NAME}.map -Wl,--gc-sections,--print-memory-usage ${UNRESOLVED_FLAG} ${VECTOR_TABLE_LINK_FLAG}"
    CACHE STRING "" FORCE
)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
