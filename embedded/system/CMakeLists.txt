set(MODULE_NAME system)

add_library(${MODULE_NAME} ${NULL_C})

target_sources(${MODULE_NAME}
    PRIVATE
        init.cc
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        osal
)
