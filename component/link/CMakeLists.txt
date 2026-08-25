set(MODULE_NAME link)
add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME}
    PRIVATE
        src/codec.cc
        src/crc16.cc
        src/router.cc
)

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

if(CONFIG_LINK_SECURITY)
    set(LINK_SECURITY_SOURCE "${PROJECT_DIR}/common/link_security.cc")
    if(NOT EXISTS "${LINK_SECURITY_SOURCE}")
        message(FATAL_ERROR
            "CONFIG_LINK_SECURITY requires product provider: ${LINK_SECURITY_SOURCE}")
    endif()
    target_sources(${MODULE_NAME} PRIVATE "${LINK_SECURITY_SOURCE}")
endif()
