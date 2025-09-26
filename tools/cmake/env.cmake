
# 定义支持的架构名称
set(ARCH_V2_NAME_LIST   arm)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TOP_DIR             ${CMAKE_SOURCE_DIR})
set(TOOLS_DIR           ${TOP_DIR}/tools)
set(BSP_DIR             ${TOP_DIR}/src/embedded)
set(APP_DIR             ${TOP_DIR}/src/app)
set(PROJECT_DIR         ${APP_DIR}/${PROJECT_NAME})
set(PROJECT_CONFIG_DIR  ${PROJECT_DIR}/config)
set(NULL_C              ${TOP_DIR}/misc/null.c)

set(BINARY_DIR_INCLUDE ${PROJECT_BINARY_DIR}/include)
set(BINARY_DIR_INCLUDE_GENERATED ${BINARY_DIR_INCLUDE}/generated)
file(MAKE_DIRECTORY ${BINARY_DIR_INCLUDE_GENERATED})

set(CMAKE_MODULE_PATH   ${CMAKE_MODULE_PATH}
                        ${TOOLS_DIR}/cmake
                        ${TOOLS_DIR}/cmake/modules
                        ${TOOLS_DIR}/cmake/toolchain
                        ${TOOLS_DIR}/cmake/toolchain/core
    )

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)