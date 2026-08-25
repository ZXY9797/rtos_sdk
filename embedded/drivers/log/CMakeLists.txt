set(MODULE_NAME log)
add_library(${MODULE_NAME} log.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        soc
        osal
)

if(CONFIG_UART)
    if(NOT TARGET uart)
        message(FATAL_ERROR "CONFIG_UART is enabled but the uart target is missing")
    endif()
    target_link_libraries(${MODULE_NAME} PRIVATE uart)
endif()
