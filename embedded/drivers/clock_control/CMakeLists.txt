set(MODULE_NAME clock_control)
add_library(${MODULE_NAME} clock_control_api.c)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${BSP_DIR}/include
    PRIVATE
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        soc
)

target_sources(${MODULE_NAME} PRIVATE st,stm32h7_clock.c)
