set(MODULE_NAME log)
add_library(${MODULE_NAME} log.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        uart
        soc
        osal
)
