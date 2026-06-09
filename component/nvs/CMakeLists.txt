set(MODULE_NAME nvs)
add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME} PRIVATE src/nvs.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        include
    PRIVATE
        ${BSP_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        osal
        drivers
)
