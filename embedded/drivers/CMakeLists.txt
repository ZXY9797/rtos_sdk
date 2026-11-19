set(MODULE_NAME drivers)
sdk_add_library(${MODULE_NAME} ${NULL_C})

foreach(family IN ITEMS gpio spi i2c uart can flash pwm adc pinctrl dma)
    string(TOUPPER "${family}" symbol)
    if(CONFIG_${symbol})
        add_subdirectory(${family})
        get_target_property(sources ${family} SOURCES)
        if(NOT sources)
            message(FATAL_ERROR "CONFIG_${symbol}=y has no compiled backend for this image")
        endif()
        target_link_libraries(drivers PUBLIC ${family})
    endif()
endforeach()

add_subdirectory_ifdef(CONFIG_IMU_ICM40609D imu)

add_subdirectory(log)

add_subdirectory(interrupt_controller)
add_subdirectory(clock_control)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        interrupt_controller
        clock_control
        log
)

