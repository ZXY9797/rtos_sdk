set(MODULE_NAME foc)
add_library(${MODULE_NAME} STATIC)

file(GLOB FOC_SOURCES src/*.cc)
target_sources(${MODULE_NAME} PRIVATE ${FOC_SOURCES})

target_include_directories(${MODULE_NAME}
    PUBLIC
        include
    PRIVATE
        ${BSP_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        osal
        drivers
        soc
)
