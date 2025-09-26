CMAKE_MINIMUM_REQUIRED(VERSION 3.20)

if(NOT DEFINED p)
    message(FATAL_ERROR "You must defined Project. eg.-Dp=xxx")
endif()

if(DEFINED t)
    set(TOOLCHAIN ${t})
else()
    set(TOOLCHAIN "armgcc")
endif()

# 工具链
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/toolchain/${TOOLCHAIN}.cmake)

set(PROJECT_NAME        ${p})

project(${PROJECT_NAME} LANGUAGES C ASM)

include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/env.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/modules/dts.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/modules/kconfig.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/tools/cmake/toolchain/compiler.cmake)

# target_compile_options(${PROJECT_NAME} INTERFACE $<$<COMPILE_LANGUAGE:ASM>:-D_ASMLANGUAGE>)

include_directories(${BINARY_DIR_INCLUDE_GENERATED})
include_directories(${BSP_DIR}/include)


add_subdirectory(${BSP_DIR}/arch)



add_executable(${PROJECT_NAME} ${PROJECT_DIR}/main.c)
target_link_libraries(${PROJECT_NAME} arch)