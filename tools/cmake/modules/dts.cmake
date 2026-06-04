# SPDX-License-Identifier: Apache-2.0

include_guard(GLOBAL)

include(extensions)
include(python)
include(pre_dt)

# The directory containing devicetree related scripts.
set(DT_SCRIPTS                  ${TOOLS_DIR}/scripts/dts)

# This parses and collects the DT information
set(GEN_EDT_SCRIPT              ${DT_SCRIPTS}/gen_edt.py)
# This generates DT information needed by the C macro APIs,
# along with a few other things.
set(GEN_DEFINES_SCRIPT          ${DT_SCRIPTS}/gen_defines.py)
# The edtlib.EDT object in pickle format.
set(EDT_PICKLE                  ${PROJECT_BINARY_DIR}/edt.pickle)
# The generated file containing the final DTS, for debugging.
set(FIRMWARE_DTS                ${PROJECT_BINARY_DIR}/firmware.dts)
# The generated C header needed by <zephyr/devicetree.h>
set(DEVICETREE_GENERATED_H      ${BINARY_DIR_INCLUDE_GENERATED}/devicetree_generated.h)
# Generated build system internals.
set(DTS_POST_CPP                ${PROJECT_BINARY_DIR}/firmware.dts.pre)
set(DTS_DEPS                    ${PROJECT_BINARY_DIR}/firmware.dts.d)

# This generates DT information needed by the Kconfig APIs.
set(GEN_DRIVER_KCONFIG_SCRIPT   ${DT_SCRIPTS}/gen_driver_kconfig_dts.py)
# Generated Kconfig symbols go here.
set(DTS_KCONFIG                 ${KCONFIG_BINARY_DIR}/Kconfig.dts)

set(DTS_SOURCE                  ${PROJECT_CONFIG_DIR}/board.dts)

set(VENDOR_PREFIXES             dts/bindings/vendor-prefixes.txt)

set(dts_files
  ${DTS_SOURCE}
  # ${board_extension_dts_files}
  # ${shield_dts_files}
  )

set(i 0)
foreach(dts_file ${dts_files})
  if(i EQUAL 0)
    message(STATUS "Found BOARD.dts: ${dts_file}")
  else()
    message(STATUS "Found devicetree overlay: ${dts_file}")
  endif()

  math(EXPR i "${i}+1")
endforeach()

if(DEFINED CMAKE_DTS_PREPROCESSOR)
  set(dts_preprocessor ${CMAKE_DTS_PREPROCESSOR})
else()
  set(dts_preprocessor ${CMAKE_C_COMPILER})
endif()
zephyr_dt_preprocess(
  CPP ${dts_preprocessor}
  SOURCE_FILES ${dts_files}
  OUT_FILE ${DTS_POST_CPP}
  DEPS_FILE ${DTS_DEPS}
  EXTRA_CPPFLAGS ${DTS_EXTRA_CPPFLAGS}
  INCLUDE_DIRECTORIES ${DTS_ROOT_SYSTEM_INCLUDE_DIRS}
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  )

toolchain_parse_make_rule(${DTS_DEPS}
  DTS_INCLUDE_FILES # Output parameter
  )

# Add the results to the list of files that, when change, force the
# build system to re-run CMake.
set_property(DIRECTORY APPEND PROPERTY
  CMAKE_CONFIGURE_DEPENDS
  ${DTS_INCLUDE_FILES}
  ${GEN_EDT_SCRIPT}
  ${GEN_DEFINES_SCRIPT}
  ${GEN_DRIVER_KCONFIG_SCRIPT}
  )

#
# Run GEN_EDT_SCRIPT.
#

unset(DTS_ROOT_BINDINGS)
foreach(dts_root ${DTS_ROOT})
  set(bindings_path ${dts_root}/dts/bindings)
  if(EXISTS ${bindings_path})
    list(APPEND
      DTS_ROOT_BINDINGS
      ${bindings_path}
      )
  endif()

  set(vendor_prefixes ${dts_root}/${VENDOR_PREFIXES})
  if(EXISTS ${vendor_prefixes})
    list(APPEND EXTRA_GEN_EDT_ARGS --vendor-prefixes ${vendor_prefixes})
  endif()
endforeach()

if(WEST_TOPDIR)
  set(GEN_EDT_WORKSPACE_DIR ${WEST_TOPDIR})
else()
  # If West is not available, define the parent directory of ZEPHYR_BASE as
  # the workspace. This will create comments that reference the files in the
  # Zephyr tree with a 'zephyr/' prefix.
  set(GEN_EDT_WORKSPACE_DIR ${TOP_DIR})
endif()

string(REPLACE ";" " " EXTRA_DTC_FLAGS_RAW "${EXTRA_DTC_FLAGS}")
set(CMD_GEN_EDT ${PYTHON_EXECUTABLE} ${GEN_EDT_SCRIPT}
--dts ${DTS_POST_CPP}
--dtc-flags '${EXTRA_DTC_FLAGS_RAW}'
--bindings-dirs ${DTS_ROOT_BINDINGS}
--workspace-dir ${GEN_EDT_WORKSPACE_DIR}
--dts-out ${FIRMWARE_DTS}.new # for debugging and dtc
--edt-pickle-out ${EDT_PICKLE}.new
${EXTRA_GEN_EDT_ARGS}
)

execute_process(
  COMMAND ${CMD_GEN_EDT}
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
  COMMAND_ERROR_IS_FATAL ANY
  )
zephyr_file_copy(${FIRMWARE_DTS}.new ${FIRMWARE_DTS} ONLY_IF_DIFFERENT)
zephyr_file_copy(${EDT_PICKLE}.new ${EDT_PICKLE} ONLY_IF_DIFFERENT)
file(REMOVE ${FIRMWARE_DTS}.new ${EDT_PICKLE}.new)
message(STATUS "Generated firmware.dts: ${FIRMWARE_DTS}")
message(STATUS "Generated pickled edt: ${EDT_PICKLE}")

#
# Run GEN_DEFINES_SCRIPT.
#

set(CMD_GEN_DEFINES ${PYTHON_EXECUTABLE} ${GEN_DEFINES_SCRIPT}
--header-out ${DEVICETREE_GENERATED_H}.new
--edt-pickle ${EDT_PICKLE}
${EXTRA_GEN_DEFINES_ARGS}
)

execute_process(
  COMMAND ${CMD_GEN_DEFINES}
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
  COMMAND_ERROR_IS_FATAL ANY
  )
zephyr_file_copy(${DEVICETREE_GENERATED_H}.new ${DEVICETREE_GENERATED_H} ONLY_IF_DIFFERENT)
file(REMOVE ${DEVICETREE_GENERATED_H}.new)
message(STATUS "Generated devicetree_generated.h: ${DEVICETREE_GENERATED_H}")

#
# Run gen_device_traits.py — 生成 DeviceTrait 特化
#

set(GEN_DEVICE_TRAITS_SCRIPT ${TOP_DIR}/tools/scripts/gen_device_traits.py)
set(DRIVERS_GENERATED_H ${BINARY_DIR_INCLUDE_GENERATED}/drivers_generated.h)
set(DTS_BINDINGS_DIR ${TOP_DIR}/embedded/dts/bindings)

execute_process(
  COMMAND ${PYTHON_EXECUTABLE} ${GEN_DEVICE_TRAITS_SCRIPT}
    ${DEVICETREE_GENERATED_H}
    ${DRIVERS_GENERATED_H}
    ${DTS_BINDINGS_DIR}
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
  RESULT_VARIABLE ret
  )
if(NOT "${ret}" STREQUAL "0")
  message(FATAL_ERROR "gen_device_traits.py failed with return code: ${ret}")
endif()
message(STATUS "Generated drivers_generated.h: ${DRIVERS_GENERATED_H}")

#
# Run GEN_DRIVER_KCONFIG_SCRIPT.
#

execute_process(
  COMMAND ${PYTHON_EXECUTABLE} ${GEN_DRIVER_KCONFIG_SCRIPT}
  --kconfig-out ${DTS_KCONFIG}
  --bindings-dirs ${DTS_ROOT_BINDINGS}
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
  RESULT_VARIABLE ret
  )
if(NOT "${ret}" STREQUAL "0")
  message(FATAL_ERROR "gen_driver_kconfig_dts.py failed with return code: ${ret}")
endif()

#
# Import devicetree contents into CMake.
# This enables the CMake dt_* API.
#
add_custom_target(devicetree_target)
zephyr_dt_import(EDT_PICKLE_FILE ${EDT_PICKLE} TARGET devicetree_target)
