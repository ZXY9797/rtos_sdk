# Kconfig consumes the hardware graph first. Then materialize this image's devices.
set(_device_tool "${TOOLS_DIR}/scripts/gen_device_traits.py")
set(_pinctrl_tool "${TOOLS_DIR}/scripts/gen_pinctrl.py")
set(_generated "${BINARY_DIR_INCLUDE_GENERATED}")
set(_device_args)
foreach(_root IN LISTS DTS_ROOT_BINDINGS)
    list(APPEND _device_args --bindings-dir "${_root}")
endforeach()
foreach(_requirement IN LISTS SDK_REQUIRED_DEVICE_CAPABILITIES)
    list(APPEND _device_args --require "${_requirement}")
endforeach()
set(SDK_IMAGE_DEVICE_TARGETS)
if(FIRMWARE_TYPE MATCHES "^(app|loader)$")
    set(SDK_IMAGE_DEVICE_TARGETS sdk::devices sdk::pinctrl)
else()
    list(APPEND _device_args --no-instances)
    if(FIRMWARE_TYPE STREQUAL "upgrade")
        set(SDK_IMAGE_DEVICE_TARGETS sdk::pinctrl)
    endif()
endif()

# These configure outputs also serve external apps and tooling. Reconfigure if a
# generator changes or an individual output is removed between builds.
set(_device_outputs "${_generated}/drivers_generated.h"
    "${_generated}/drivers_generated.cc" "${_generated}/pinctrl_generated.cc"
    "${_generated}/devices.json")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${_device_tool}" "${_pinctrl_tool}" "${TOOLS_DIR}/scripts/device_bindings.py"
    "${TOOLS_DIR}/scripts/device_codegen_common.py" ${_device_outputs})
execute_process(COMMAND "${PYTHON_EXECUTABLE}" "${_device_tool}"
    --edt-pickle "${EDT_PICKLE}" --config "${DOTCONFIG}" ${_device_args}
    --header-out "${_generated}/drivers_generated.h"
    --source-out "${_generated}/drivers_generated.cc"
    --report-out "${_generated}/devices.json" COMMAND_ERROR_IS_FATAL ANY)
execute_process(COMMAND "${PYTHON_EXECUTABLE}" "${_pinctrl_tool}"
    --edt-pickle "${EDT_PICKLE}" --output "${_generated}/pinctrl_generated.cc"
    COMMAND_ERROR_IS_FATAL ANY)
add_custom_target(device_codegen DEPENDS ${_device_outputs})
foreach(_kind IN ITEMS devices pinctrl)
    add_library(sdk_${_kind} INTERFACE)
    add_library(sdk::${_kind} ALIAS sdk_${_kind})
endforeach()
target_sources(sdk_devices INTERFACE "${_generated}/drivers_generated.cc")
target_sources(sdk_pinctrl INTERFACE "${_generated}/pinctrl_generated.cc")
