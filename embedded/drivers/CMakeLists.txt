set(MODULE_NAME drivers)
add_library(${MODULE_NAME} ${NULL_C})

target_include_directories(${MODULE_NAME}
    PUBLIC
    PRIVATE
)

add_subdirectory_ifdef(CONFIG_GPIO gpio)
target_link_libraries_ifdef(CONFIG_GPIO ${MODULE_NAME} PUBLIC gpio)

add_subdirectory(interrupt_controller)
add_subdirectory(clock_control)
add_subdirectory(timer)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        interrupt_controller
        clock_control
        timer
    PRIVATE
)

