include_guard(GLOBAL)

include(extensions)
include(python)

# autoconf.h 由Kconfig生成并放在
# <build>/include/generated/autoconf.h.
# 项目可以自定义一个自己的位置并定义在AUTOCONF_H中在使用该模块前
set_ifndef(AUTOCONF_H ${BINARY_DIR_INCLUDE_GENERATED}/autoconf.h)
# Re-configure (Re-execute all CMakeLists.txt code) when autoconf.h changes
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${AUTOCONF_H})

set_ifndef(KCONFIG_NAMESPACE "CONFIG")

set(KCONFIG_ROOT                ${TOP_DIR}/Kconfig)
set(DOTCONFIG                   ${PROJECT_BINARY_DIR}/.config)
set(PARSED_KCONFIG_SOURCES_TXT  ${KCONFIG_BINARY_DIR}/sources.txt)

set(COMMON_KCONFIG_ENV_SETTINGS
  PYTHON_EXECUTABLE=${PYTHON_EXECUTABLE}
  srctree=${TOP_DIR}
  EDT_PICKLE=${EDT_PICKLE}
  SOC_YAML_FILE=${BSP_DIR}/soc/soc.yml
  KCONFIG_BINARY_DIR=${KCONFIG_BINARY_DIR}
  APPLICATION_SOURCE_DIR=${APPLICATION_SOURCE_DIR}
  PROJECT_DIR=${PROJECT_DIR}
)

set(EXTRA_KCONFIG_TARGET_COMMAND_FOR_menuconfig
  ${TOOLS_DIR}/scripts/kconfig/guiconfig.py
  )

set(EXTRA_KCONFIG_TARGET_COMMAND_FOR_config
  ${TOOLS_DIR}/scripts/kconfig/guiconfig.py
  )

set(EXTRA_KCONFIG_TARGET_COMMAND_FOR_guiconfig
  ${TOOLS_DIR}/scripts/kconfig/guiconfig.py
  )

set(EXTRA_KCONFIG_TARGET_COMMAND_FOR_hardenconfig
  ${TOOLS_DIR}/scripts/kconfig/hardenconfig.py
  )

set_ifndef(KCONFIG_TARGETS menuconfig config guiconfig hardenconfig)

# winpty is an optional dependency
find_program(PTY_INTERFACE winpty)
if("${PTY_INTERFACE}" STREQUAL "PTY_INTERFACE-NOTFOUND")
  set(PTY_INTERFACE "")
endif()

foreach(kconfig_target
    ${KCONFIG_TARGETS}
    )
  add_custom_target(
    ${kconfig_target}
    ${CMAKE_COMMAND} -E env
    ${COMMON_KCONFIG_ENV_SETTINGS}
    DTS_POST_CPP=${DTS_POST_CPP}
    DTS_ROOT_BINDINGS=${DTS_ROOT_BINDINGS}
    ${PTY_INTERFACE}
    ${PYTHON_EXECUTABLE}
    ${EXTRA_KCONFIG_TARGET_COMMAND_FOR_${kconfig_target}}
    ${KCONFIG_ROOT}
    WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
    USES_TERMINAL
    COMMAND_EXPAND_LISTS
    )
endforeach()

# Bring in extra configuration files dropped in by the user or anyone else;
# make sure they are set at the end so we can override any other setting
file(GLOB config_files ${PROJECT_CONFIG_DIR}/*.conf)
list(SORT config_files)
set(
  merge_config_files
  ${config_files}
)

# Create a list of absolute paths to the .config sources from
# merge_config_files, which is a mix of absolute and relative paths.
set(merge_config_files_with_absolute_paths "")
foreach(f ${merge_config_files})
  if(IS_ABSOLUTE ${f})
    set(path ${f})
  else()
    set(path ${APPLICATION_CONFIG_DIR}/${f})
  endif()

  list(APPEND merge_config_files_with_absolute_paths ${path})
endforeach()
set(merge_config_files ${merge_config_files_with_absolute_paths})

foreach(f ${merge_config_files})
  if(NOT EXISTS ${f} OR IS_DIRECTORY ${f})
    message(FATAL_ERROR "File not found: ${f}")
  endif()
endforeach()

# Calculate a checksum of merge_config_files to determine if we need
# to re-generate .config
set(merge_config_files_checksum "")
foreach(f ${merge_config_files})
  file(MD5 ${f} checksum)
  set(merge_config_files_checksum "${merge_config_files_checksum}${checksum}")
endforeach()

# Add to the checksum all the Kconfig files which were used last time
set(merge_kconfig_checksum "")
if(EXISTS ${PARSED_KCONFIG_SOURCES_TXT})
  file(STRINGS ${PARSED_KCONFIG_SOURCES_TXT} parsed_kconfig_sources_list ENCODING UTF-8)
  foreach(f ${parsed_kconfig_sources_list})
    if(EXISTS ${f})
      file(MD5 ${f} checksum)
      set(merge_kconfig_checksum "${merge_kconfig_checksum}${checksum}")
    endif()
  endforeach()
endif()

# Create a new .config if it does not exists, or if the checksum of
# the dependencies has changed
set(merge_config_files_checksum_file ${PROJECT_BINARY_DIR}/.cmake.dotconfig.checksum)
set(CREATE_NEW_DOTCONFIG 1)
# Check if the checksum file exists too before trying to open it, though it
# should under normal circumstances
if(EXISTS ${DOTCONFIG} AND EXISTS ${merge_config_files_checksum_file})
  # Read out what the checksum was previously
  file(READ
    ${merge_config_files_checksum_file}
    merge_config_files_checksum_prev
    )
  if(
      ${merge_config_files_checksum}${merge_kconfig_checksum} STREQUAL
      ${merge_config_files_checksum_prev}
      )
    # Checksum is the same as before
    set(CREATE_NEW_DOTCONFIG 0)
  endif()
endif()

# Create a new .config if it does not exists
set(CREATE_NEW_DOTCONFIG 1)
if(EXISTS ${DOTCONFIG})
    set(CREATE_NEW_DOTCONFIG 0)
endif()

if(CREATE_NEW_DOTCONFIG)
  set(input_configs_flags --handwritten-input-configs)
  set(input_configs ${merge_config_files})
else()
  set(input_configs ${DOTCONFIG})
endif()

cmake_path(GET AUTOCONF_H PARENT_PATH autoconf_h_path)
if(NOT EXISTS ${autoconf_h_path})
  file(MAKE_DIRECTORY ${autoconf_h_path})
endif()

execute_process(
  COMMAND ${CMAKE_COMMAND} -E env
  ${COMMON_KCONFIG_ENV_SETTINGS}
  ${PYTHON_EXECUTABLE}
  ${TOOLS_DIR}/scripts/kconfig/kconfig.py
  ${input_configs_flags}
  ${KCONFIG_ROOT}
  ${DOTCONFIG}
  ${AUTOCONF_H}
  ${PARSED_KCONFIG_SOURCES_TXT}
  ${input_configs}
  WORKING_DIRECTORY ${TOP_DIR}
  # The working directory is set to the app dir such that the user
  # can use relative paths in CONF_FILE, e.g. CONF_FILE=nrf5.conf
  RESULT_VARIABLE ret
  )
if(NOT "${ret}" STREQUAL "0")
  message(FATAL_ERROR "command failed with return code: ${ret}")
endif()

# Read out the list of 'Kconfig' sources that were used by the engine.
file(STRINGS ${PARSED_KCONFIG_SOURCES_TXT} parsed_kconfig_sources_list ENCODING UTF-8)

# Recalculate the Kconfig files' checksum, since the list of files may have
# changed.
set(merge_kconfig_checksum "")
foreach(f ${parsed_kconfig_sources_list})
  file(MD5 ${f} checksum)
  set(merge_kconfig_checksum "${merge_kconfig_checksum}${checksum}")
endforeach()

# Force CMAKE configure when the Kconfig sources or configuration files changes.
foreach(kconfig_input
    ${merge_config_files}
    ${DOTCONFIG}
    ${parsed_kconfig_sources_list}
    )
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${kconfig_input})
endforeach()

if(CREATE_NEW_DOTCONFIG)
  # Write the new configuration fragment checksum. Only do this if kconfig.py
  # succeeds, to avoid marking zephyr/.config as up-to-date when it hasn't been
  # regenerated.
  file(WRITE ${merge_config_files_checksum_file}
             ${merge_config_files_checksum}${merge_kconfig_checksum})
endif()

add_custom_target(config-twister DEPENDS ${DOTCONFIG})

# Import the .config file and make all settings available in CMake processing.
import_kconfig(${KCONFIG_NAMESPACE} ${DOTCONFIG})
