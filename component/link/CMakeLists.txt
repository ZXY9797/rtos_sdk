set(MODULE_NAME link)
sdk_add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME}
    PRIVATE
        src/codec.cc
        src/crc8.cc
        src/crc16.cc
        src/diagnostics.cc
        src/router.cc
)
target_sources_ifdef(CONFIG_LINK_SYSTEM_DIAGNOSTICS ${MODULE_NAME}
    PRIVATE src/system_diagnostics.cc)
target_link_libraries_ifdef(CONFIG_LINK_SYSTEM_DIAGNOSTICS ${MODULE_NAME}
    PRIVATE runtime)

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
