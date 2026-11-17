include_guard(GLOBAL)

# Resolve explicit fragments consistently for CLI and independent applications.
# Repository-relative paths take precedence, followed by image config paths.
function(sdk_resolve_image_file input output)
    if(IS_ABSOLUTE "${input}")
        set(_path "${input}")
    elseif(EXISTS "${TOP_DIR}/${input}")
        set(_path "${TOP_DIR}/${input}")
    else()
        set(_path "${PROJECT_CONFIG_DIR}/${input}")
    endif()
    if(NOT EXISTS "${_path}" OR IS_DIRECTORY "${_path}")
        message(FATAL_ERROR "Image configuration file not found: ${input}")
    endif()
    file(REAL_PATH "${_path}" _path)
    set(${output} "${_path}" PARENT_SCOPE)
endfunction()

function(sdk_select_image_configuration)
    set(_configs)
    if(DEFINED BOARD_DIR AND EXISTS "${BOARD_DIR}/board_defconfig")
        list(APPEND _configs "${BOARD_DIR}/board_defconfig")
    endif()
    if(DEFINED CONF_FILE)
        list(APPEND _configs ${CONF_FILE})
    else()
        list(APPEND _configs "${PROJECT_CONFIG_DIR}/prj.conf")
    endif()
    if(NOT PROFILE STREQUAL "default")
        set(_profile "${PROJECT_CONFIG_DIR}/profiles/${PROFILE}.conf")
        if(NOT EXISTS "${_profile}")
            message(FATAL_ERROR "Missing ${PROFILE} profile for ${PROJECT_CONFIG_DIR}")
        endif()
        list(APPEND _configs "${_profile}")
    endif()
    list(APPEND _configs ${EXTRA_CONF_FILE})

    set(_resolved)
    foreach(_file IN LISTS _configs)
        sdk_resolve_image_file("${_file}" _path)
        list(APPEND _resolved "${_path}")
    endforeach()
    set(SDK_IMAGE_CONFIG_FILES "${_resolved}" PARENT_SCOPE)

    set(_overlays)
    set(_default_overlay "${PROJECT_CONFIG_DIR}/${FIRMWARE_TYPE}.overlay")
    if(EXISTS "${_default_overlay}")
        list(APPEND _overlays "${_default_overlay}")
    endif()
    foreach(_file IN LISTS DTC_OVERLAY_FILE)
        sdk_resolve_image_file("${_file}" _path)
        list(APPEND _overlays "${_path}")
    endforeach()
    set(SDK_IMAGE_OVERLAYS "${_overlays}" PARENT_SCOPE)

    # Watch directory membership as well as file contents: adding/removing an
    # optional default overlay must also trigger CMake regeneration.
    file(GLOB _image_inputs CONFIGURE_DEPENDS
         "${PROJECT_CONFIG_DIR}/*.overlay"
         "${PROJECT_CONFIG_DIR}/profiles/*.conf")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
                 ${_resolved} ${_overlays})
endfunction()
