set(MODULE_NAME system)

add_library(${MODULE_NAME} ${NULL_C})

target_sources(${MODULE_NAME}
    PRIVATE
        init.cc
        fault.cc
        assert.cc
)

target_include_directories(${MODULE_NAME}
    PRIVATE
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        osal
        soc
)
