set(MODULE_NAME link)
add_library(${MODULE_NAME} STATIC)

file(GLOB LINK_SOURCES src/*.cc)
target_sources(${MODULE_NAME} PRIVATE ${LINK_SOURCES})

target_include_directories(${MODULE_NAME}
    PUBLIC
        include
    PRIVATE
        ${BSP_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        osal
        drivers
        soc
)
