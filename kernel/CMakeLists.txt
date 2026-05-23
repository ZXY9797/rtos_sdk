# Kernel CMakeLists.txt
# This file implements the kernel build for MSDK

set(KERNEL_NAME kernel)
add_library(${KERNEL_NAME} ${NULL_C})

# Add kernel source files
target_sources(${KERNEL_NAME} PRIVATE
    init.c
    thread.c
    sched.c
    sem.c
    mutex.c
    mempool.c
    kheap.c
    timeout.c
    device.c
)

# Add userspace sources if enabled
if(CONFIG_USERSPACE)
    target_sources(${KERNEL_NAME} PRIVATE
        userspace.c
        mem_domain.c
    )
endif()

# Include directories
target_include_directories(${KERNEL_NAME}
    PUBLIC
        include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Add architecture subdirectory
add_subdirectory(arch)

# Link with architecture library
target_link_libraries(${KERNEL_NAME}
    PUBLIC
        arch
)
