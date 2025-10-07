set(MODULE_NAME irq_controller)
add_library(${MODULE_NAME} irq_controller_api.c)

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
)

target_sources(${MODULE_NAME} PRIVATE arm,v7m_nvic.c)
