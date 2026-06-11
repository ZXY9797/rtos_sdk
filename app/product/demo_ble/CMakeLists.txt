set(MODULE_NAME app)
add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${TOP_DIR}/component/ble/include
        ${TOP_DIR}/component/ble/goodix/sdk
)

target_sources(${MODULE_NAME}
    PRIVATE
        main.cc
        ble_app.cc
        ${DRIVERS_GENERATED_CC}
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
    PRIVATE
        drivers
        osal
        -Wl,--whole-archive
        ble_gr5525
        -Wl,--no-whole-archive
)

if(TARGET link)
    target_link_libraries(${MODULE_NAME} PRIVATE link)
endif()
