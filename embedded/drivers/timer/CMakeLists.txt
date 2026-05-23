set(MODULE_NAME timer)
add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
    PRIVATE
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        cmsis
        soc
        arch
        kernel
)

target_sources(${MODULE_NAME} PRIVATE cortex_m_systick.c)
