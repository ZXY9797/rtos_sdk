set(MODULE_NAME boot_common)
add_library(${MODULE_NAME} STATIC)

target_sources(${MODULE_NAME}
    PRIVATE
        src/boot_ctrl.cc
        src/boot_confirm.cc
        src/boot_flash.cc
        src/dfu_crc.cc
        src/sha256.cc
)

target_include_directories(${MODULE_NAME}
    PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/../include
)

target_link_libraries(${MODULE_NAME}
    PUBLIC
        drivers
)
