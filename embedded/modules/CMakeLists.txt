set(MODULE_NAME modules)
add_library(${MODULE_NAME} ${NULL_C})

add_subdirectory(cmsis)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        cmsis
)
