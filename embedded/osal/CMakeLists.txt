set(MODULE_NAME osal)
add_library(${MODULE_NAME} ${NULL_C})

if(CONFIG_RTOS_FREERTOS)
    set(RTOS_TYPE freertos)
else()
    set(RTOS_TYPE rt-thread)
endif()

add_subdirectory(${RTOS_TYPE})

target_sources(${MODULE_NAME} PRIVATE mem.cc)

# 添加 C++ OSAL 头文件路径
target_include_directories(${MODULE_NAME} PUBLIC ${CMAKE_SOURCE_DIR}/embedded/include)
