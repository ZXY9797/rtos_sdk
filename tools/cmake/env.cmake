
# 定义支持的架构名称
set(ARCH_V2_NAME_LIST   arm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TOP_DIR             ${CMAKE_SOURCE_DIR})
set(TOOLS_DIR           ${TOP_DIR}/tools)
set(BSP_DIR             ${TOP_DIR}/embedded)
set(APP_DIR             ${TOP_DIR}/app)
set(PROJECT_DIR         ${APP_DIR}/${PROJECT_NAME})
set(PROJECT_CONFIG_DIR  ${PROJECT_DIR}/config)
set(NULL_C              ${TOOLS_DIR}/cmake/null.c)

set(BINARY_DIR_INCLUDE ${PROJECT_BINARY_DIR}/include)
set(BINARY_DIR_INCLUDE_GENERATED ${BINARY_DIR_INCLUDE}/generated)
file(MAKE_DIRECTORY ${BINARY_DIR_INCLUDE_GENERATED})

set(KCONFIG_BINARY_DIR ${PROJECT_BINARY_DIR}/Kconfig)
file(MAKE_DIRECTORY ${KCONFIG_BINARY_DIR})

set(CMAKE_MODULE_PATH   ${CMAKE_MODULE_PATH}
                        ${TOOLS_DIR}/cmake
                        ${TOOLS_DIR}/cmake/modules
                        ${TOOLS_DIR}/cmake/toolchain
                        ${TOOLS_DIR}/cmake/toolchain/core
    )

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)