set(_metadata_command ${PYTHON_EXECUTABLE} "${TOOLS_DIR}/scripts/sdk_artifacts.py"
    --root "${TOP_DIR}" --application "${PROJECT_DIR}" --build "${PROJECT_BINARY_DIR}"
    --compiler "${CMAKE_C_COMPILER}" --layout "${BOOT_LAYOUT_MANIFEST}"
    --product "${PRODUCT_NAME}" --firmware "${FIRMWARE_TYPE}" --profile "${PROFILE}"
    --board "${PRODUCT_NAME}" --base-board "${SDK_BASE_BOARD}")
if(SDK_APPLICATION_ID)
    list(APPEND _metadata_command --application-id "${SDK_APPLICATION_ID}")
endif()
foreach(_config IN LISTS SDK_IMAGE_CONFIG_FILES)
    list(APPEND _metadata_command --config-fragment "${_config}")
endforeach()
foreach(_overlay IN ITEMS "${SDK_BOARD_DTS}" ${SDK_IMAGE_OVERLAYS} "${BOOT_PARTITIONS_DTS}")
    list(APPEND _metadata_command --overlay "${_overlay}")
endforeach()
foreach(_module IN LISTS MODULE_ROOTS)
    list(APPEND _metadata_command --module-root "${_module}")
endforeach()
set(_native_deps "${PROJECT_BINARY_DIR}/native-dts-inputs.txt")
execute_process(COMMAND ${_metadata_command} --native-deps "${DTS_POST_CPP}" "${_native_deps}"
                COMMAND_ERROR_IS_FATAL ANY)
file(STRINGS "${_native_deps}" _native_inputs ENCODING UTF-8)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${TOOLS_DIR}/scripts/sdk_artifacts.py" ${_native_inputs})
add_custom_target(build_metadata
    COMMAND ${_metadata_command}
    BYPRODUCTS "${PROJECT_BINARY_DIR}/include/generated/build_info.h"
               "${PROJECT_BINARY_DIR}/build-info.json"
    VERBATIM)
add_dependencies(system build_metadata)
add_dependencies(system_fault build_metadata)
add_dependencies(app build_metadata)
set(_resource_args --layout "${BOOT_LAYOUT_MANIFEST}")
if(SDK_RESOURCE_BASELINE)
    get_filename_component(_baseline "${SDK_RESOURCE_BASELINE}" ABSOLUTE BASE_DIR "${TOP_DIR}")
    if(NOT EXISTS "${_baseline}")
        message(FATAL_ERROR "Missing resource baseline: ${_baseline}")
    endif()
    list(APPEND _resource_args --baseline "${_baseline}")
    set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS "${_baseline}")
endif()
set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${TOOLS_DIR}/scripts/sdk_artifacts.py" "${BOOT_LAYOUT_MANIFEST}")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${PYTHON_EXECUTABLE} "${TOOLS_DIR}/scripts/sdk_artifacts.py"
            --build "${PROJECT_BINARY_DIR}" --elf "$<TARGET_FILE:${PROJECT_NAME}>" ${_resource_args}
    COMMENT "Generating device graph, ELF resources and firmware provenance" VERBATIM)
