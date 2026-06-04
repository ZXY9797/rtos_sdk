set(MODULE_NAME app)
add_library(${MODULE_NAME} main.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        .
    PRIVATE
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        embedded
    PRIVATE
)

