set(MODULE_NAME osal)
add_library(${MODULE_NAME} ${NULL_C})

set(RTOS_TYPE rt-thread)
add_subdirectory(${RTOS_TYPE})
