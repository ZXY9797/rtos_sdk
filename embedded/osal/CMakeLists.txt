set(MODULE_NAME osal)
add_library(${MODULE_NAME} ${NULL_C})

if(CONFIG_RTOS_FREERTOS)
    set(RTOS_TYPE freertos)
else()
    set(RTOS_TYPE rt-thread)
endif()

add_subdirectory(${RTOS_TYPE})
