set(MODULE_NAME drivers)
add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
    PRIVATE
)

add_subdirectory(irq_controller)
add_subdirectory(clock_control)
add_subdirectory(gpio)
add_subdirectory(timer)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        irq_controller
        clock_control
        gpio
        timer
    PRIVATE
)

