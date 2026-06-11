set(MODULE_NAME pinctrl)
add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME} PRIVATE pinctrl_common.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
)
