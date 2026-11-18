set(_metadata_command ${PYTHON_EXECUTABLE} "${TOOLS_DIR}/scripts/sdk_artifacts.py"
    --root "${TOP_DIR}" --application "${PROJECT_DIR}" --build "${PROJECT_BINARY_DIR}"
    --compiler "${CMAKE_C_COMPILER}" --layout "${BOOT_LAYOUT_MANIFEST}"
    --product "${PRODUCT_NAME}" --firmware "${FIRMWARE_TYPE}" --profile "${PROFILE}"
    --board "${PRODUCT_NAME}")
foreach(_config IN LISTS SDK_IMAGE_CONFIG_FILES)
    list(APPEND _metadata_command --config-fragment "${_config}")
endforeach()
foreach(_overlay IN ITEMS "${SDK_BOARD_DTS}" ${SDK_IMAGE_OVERLAYS} "${BOOT_PARTITIONS_DTS}")
    list(APPEND _metadata_command --overlay "${_overlay}")
endforeach()
foreach(_module IN LISTS MODULE_ROOTS)
    list(APPEND _metadata_command --module-root "${_module}")
endforeach()
execute_process(COMMAND ${_metadata_command} COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(build_metadata
    COMMAND ${_metadata_command}
    BYPRODUCTS "${PROJECT_BINARY_DIR}/include/generated/build_info.h"
               "${PROJECT_BINARY_DIR}/build-info.json"
    VERBATIM)
add_dependencies(system build_metadata device_codegen)
add_dependencies(system_fault build_metadata)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${PYTHON_EXECUTABLE} "${TOOLS_DIR}/scripts/sdk_artifacts.py"
            --build "${PROJECT_BINARY_DIR}" --elf "$<TARGET_FILE:${PROJECT_NAME}>"
    COMMENT "Generating device graph, ELF resources and firmware provenance" VERBATIM)
