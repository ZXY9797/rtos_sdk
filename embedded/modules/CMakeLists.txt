set(MODULE_NAME modules)
add_library(${MODULE_NAME} ${NULL_C})

add_subdirectory(cmsis)
add_subdirectory(lwmem)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        cmsis
        lwmem
)
