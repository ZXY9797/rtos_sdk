set(MODULE_NAME lwmem)

add_library(${MODULE_NAME})

target_sources(${MODULE_NAME}
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/lwmem/src/lwmem/lwmem.c
        ${CMAKE_CURRENT_LIST_DIR}/config/lwmem_sys_rtos_sdk.cc
)

target_include_directories(${MODULE_NAME}
    PRIVATE
        ${BSP_DIR}/include
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/lwmem/src/include
        ${CMAKE_CURRENT_LIST_DIR}/config
)

target_link_libraries(${MODULE_NAME} PRIVATE osal)

