set(MODULE_NAME foc)
sdk_add_library(${MODULE_NAME} STATIC ${NULL_C})

target_sources_ifdef(CONFIG_FOC_CURRENT_CONTROL ${MODULE_NAME}
    PRIVATE
        src/motor.cc
)

target_sources_ifdef(CONFIG_FOC_VOLTAGE_CONTROL ${MODULE_NAME}
    PRIVATE
        src/voltage_motor.cc)

target_include_directories(${MODULE_NAME}
    PUBLIC
        include
    PRIVATE
        ${BSP_DIR}/include
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        drivers
        osal
        sdk::motor
)

if(CONFIG_FOC_CURRENT_CONTROL)
    target_link_libraries(${MODULE_NAME}
        PRIVATE
            soc)
endif()
