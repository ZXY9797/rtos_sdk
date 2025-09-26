set(MODULE_NAME arch)
add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
        .
        ./CMSIS/Include
    PRIVATE
)
