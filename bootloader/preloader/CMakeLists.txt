cmake_minimum_required(VERSION 3.20)

set(MODULE_NAME app)
add_library(${MODULE_NAME} ${NULL_C})

set(BOOT_SECURITY_SOURCE ${PROJECT_BOOT_COMMON_DIR}/boot_security.cc)
if(CONFIG_BOOT_PRODUCTION AND NOT EXISTS "${BOOT_SECURITY_SOURCE}")
    message(FATAL_ERROR
        "Production preloader requires product root-of-trust provider: ${BOOT_SECURITY_SOURCE}")
endif()

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE
        ${BSP_DIR}/include
        ${BSP_DIR}/modules/cmsis/CMSIS/Core/Include
        ${BSP_DIR}/modules/cmsis
        ${PROJECT_PRODUCT_DIR}/include
)

target_sources(${MODULE_NAME}
    PRIVATE
        src/preloader_init.cc
        src/preloader_main.cc
)

if(CONFIG_BOOT_PRODUCTION)
    target_sources(${MODULE_NAME} PRIVATE ${BOOT_SECURITY_SOURCE})
endif()

target_link_libraries(${MODULE_NAME}
    PRIVATE
        boot_common
        soc
)
