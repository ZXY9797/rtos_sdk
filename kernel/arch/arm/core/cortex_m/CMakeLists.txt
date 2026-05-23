# ARM Cortex-M core CMakeLists.txt

add_library(cortex_m STATIC
    irq_manage.c
)

target_link_libraries(cortex_m PUBLIC kernel_interface)
