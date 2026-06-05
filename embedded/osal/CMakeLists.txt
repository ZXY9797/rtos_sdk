set(MODULE_NAME osal)
add_library(${MODULE_NAME} ${NULL_C})

if(CONFIG_RTOS_FREERTOS)
    set(RTOS_TYPE freertos)
else()
    set(RTOS_TYPE rt-thread)
endif()

add_subdirectory(${RTOS_TYPE})

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_SOURCE_DIR}/embedded/include       # 公共头文件
        ${CMAKE_SOURCE_DIR}/embedded/include/osal   # osal.h / osal_types.h
        ${CMAKE_CURRENT_SOURCE_DIR}/${RTOS_TYPE}     # OS 差异 osal_types.h
)
