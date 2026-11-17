set(MODULE_NAME osal)
sdk_add_library(${MODULE_NAME} ${NULL_C})

if(CONFIG_RTOS_BAREMETAL)
    set(RTOS_TYPE baremetal)
elseif(CONFIG_RTOS_FREERTOS)
    set(RTOS_TYPE freertos)
elseif(CONFIG_RTOS_RT_THREAD)
    set(RTOS_TYPE rt-thread)
else()
    message(FATAL_ERROR "Exactly one supported RTOS backend must be selected")
endif()

add_subdirectory(${RTOS_TYPE})

if(CONFIG_OSAL_HAS_THREADS)
    target_sources(${MODULE_NAME} PRIVATE event_group.cc)
endif()

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${TOP_DIR}/embedded/include       # 公共头文件
        ${TOP_DIR}/embedded/include/osal   # osal.h / osal_types.h
        ${CMAKE_CURRENT_SOURCE_DIR}/${RTOS_TYPE}     # OS 差异 osal_types.h
)
