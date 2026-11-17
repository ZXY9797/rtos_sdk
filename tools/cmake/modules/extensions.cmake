# SPDX-License-Identifier: Apache-2.0
# Reduced from the previously vendored Zephyr helpers; see upstream-helpers.json.
include_guard(GLOBAL)

function(import_kconfig prefix kconfig_fragment)
  cmake_parse_arguments(IMPORT_KCONFIG "" "TARGET" "" ${ARGN})
  file(
    STRINGS
    ${kconfig_fragment}
    DOT_CONFIG_LIST
    ENCODING "UTF-8"
  )

  foreach (LINE ${DOT_CONFIG_LIST})
    if("${LINE}" MATCHES "^(${prefix}[^=]+)=([ymn]|.+$)")
      # Matched a normal value assignment, like: CONFIG_NET_BUF=y
      # Note: if the value starts with 'y', 'm', or 'n', then we assume it's a
      # bool or tristate (we don't know the type from <kconfig_fragment> alone)
      # and we only match the first character. This is to align with Kconfiglib.
      set(CONF_VARIABLE_NAME "${CMAKE_MATCH_1}")
      set(CONF_VARIABLE_VALUE "${CMAKE_MATCH_2}")
    elseif("${LINE}" MATCHES "^# (${prefix}[^ ]+) is not set")
      # Matched something like: # CONFIG_FOO is not set
      # This is interpreted as: CONFIG_FOO=n
      set(CONF_VARIABLE_NAME "${CMAKE_MATCH_1}")
      set(CONF_VARIABLE_VALUE "n")
    else()
      # Ignore this line.
      # Note: we also ignore assignments which don't have the desired <prefix>.
      continue()
    endif()

    # If the provided value is n, then the corresponding CMake variable or
    # target property will be unset.
    if("${CONF_VARIABLE_VALUE}" STREQUAL "n")
      if(DEFINED IMPORT_KCONFIG_TARGET)
        set_property(TARGET ${IMPORT_KCONFIG_TARGET} PROPERTY "${CONF_VARIABLE_NAME}")
      else()
        unset("${CONF_VARIABLE_NAME}" PARENT_SCOPE)
      endif()
      list(REMOVE_ITEM keys "${CONF_VARIABLE_NAME}")
      continue()
    endif()

    # Otherwise, the variable/property will be set to the provided value.
    # For string values, we also remove the surrounding quotation marks.
    if("${CONF_VARIABLE_VALUE}" MATCHES "^\"(.*)\"$")
      set(CONF_VARIABLE_VALUE ${CMAKE_MATCH_1})
    endif()

    if(DEFINED IMPORT_KCONFIG_TARGET)
      set_property(TARGET ${IMPORT_KCONFIG_TARGET} PROPERTY "${CONF_VARIABLE_NAME}" "${CONF_VARIABLE_VALUE}")
    else()
      set("${CONF_VARIABLE_NAME}" "${CONF_VARIABLE_VALUE}" PARENT_SCOPE)
    endif()
    list(APPEND keys "${CONF_VARIABLE_NAME}")
  endforeach()

  if(DEFINED IMPORT_KCONFIG_TARGET)
    set_property(TARGET ${IMPORT_KCONFIG_TARGET} PROPERTY "kconfigs" "${keys}")
  endif()

  list(LENGTH IMPORT_KCONFIG_UNPARSED_ARGUMENTS unparsed_length)
  if(unparsed_length GREATER 0)
    if(unparsed_length GREATER 1)
    # Two mandatory arguments and one optional, anything after that is an error.
      list(GET IMPORT_KCONFIG_UNPARSED_ARGUMENTS 1 first_invalid)
      message(FATAL_ERROR "Unexpected argument after '<keys>': import_kconfig(... ${first_invalid})")
    endif()
    set(${IMPORT_KCONFIG_UNPARSED_ARGUMENTS} "${keys}" PARENT_SCOPE)
  endif()
endfunction()

function(add_subdirectory_ifdef feature_toggle source_dir)
  if(${${feature_toggle}})
    add_subdirectory(${source_dir} ${ARGN})
  endif()
endfunction()

function(target_sources_ifdef feature_toggle target scope item)
  if(${${feature_toggle}})
    target_sources(${target} ${scope} ${item} ${ARGN})
  endif()
endfunction()

function(target_include_directories_ifdef feature_toggle target scope item)
  if(${${feature_toggle}})
    target_include_directories(${target} ${scope} ${item} ${ARGN})
  endif()
endfunction()

function(target_link_libraries_ifdef feature_toggle target item)
  if(${${feature_toggle}})
    target_link_libraries(${target} ${item} ${ARGN})
  endif()
endfunction()

function(set_ifndef variable value)
  if(NOT ${variable})
    set(${variable} ${value} ${ARGN} PARENT_SCOPE)
  endif()
endfunction()

function(toolchain_parse_make_rule input_file include_files)
  file(STRINGS ${input_file} input)

  # The file is formatted like this:
  # empty_file.o: misc/empty_file.c \
  # nrf52840dk_nrf52840/nrf52840dk_nrf52840.dts \
  # nrf52840_qiaa.dtsi

  # The dep file will contain `\` for line continuation.
  # This results in `\;` which is then treated a the char `;` instead of
  # the element separator, so let's get the pure `;` back.
  string(REPLACE "\;" ";" input_as_list ${input})

  # The file might also contain multiple files on one line if one or both of
  # the file paths are short, split these up into multiple elements using regex
  string(REGEX REPLACE "([^ ])[ ]([^ ])" "\\1;\\2" input_as_list "${input_as_list}")

  # Pop the first item containing "empty_file.o:"
  list(POP_FRONT input_as_list first_input_line)

  # Remove whitespace before and after filename and convert to CMake path.
  foreach(file ${input_as_list})
    string(STRIP "${file}" file)
    file(TO_CMAKE_PATH "${file}" file)
    list(APPEND result "${file}")
  endforeach()

  set(${include_files} ${result} PARENT_SCOPE)
endfunction()

function(zephyr_file_copy oldname newname)
  set(options ONLY_IF_DIFFERENT)
  cmake_parse_arguments(ZEPHYR_FILE_COPY "${options}" "" "" ${ARGN})

  if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.21.0)
    if(ZEPHYR_FILE_COPY_ONLY_IF_DIFFERENT)
      set(copy_file_options ONLY_IF_DIFFERENT)
    endif()
    file(COPY_FILE ${oldname} ${newname} ${copy_file_options})
  else()
    if(ZEPHYR_FILE_COPY_ONLY_IF_DIFFERENT)
      set(copy_file_command copy_if_different)
    else()
      set(copy_file_command copy)
    endif()
    execute_process(
      COMMAND ${CMAKE_COMMAND} -E ${copy_file_command} ${oldname} ${newname}
    )
  endif()
endfunction()

function(dt_nodelabel var)
  set(options "REQUIRED")
  set(req_single_args "NODELABEL")
  set(single_args "TARGET")
  cmake_parse_arguments(DT_LABEL "${options}" "${req_single_args};${single_args}" "" ${ARGN})

  dt_target_internal(DT_LABEL_TARGET)

  if(${ARGV0} IN_LIST req_single_args)
    message(FATAL_ERROR "dt_nodelabel(${ARGV0} ...) missing return parameter.")
  endif()

  foreach(arg ${req_single_args})
    if(NOT DEFINED DT_LABEL_${arg})
      message(FATAL_ERROR "dt_nodelabel(${ARGV0} ...) "
                          "missing required argument: ${arg}"
      )
    endif()
  endforeach()

  get_target_property(${var} ${DT_LABEL_TARGET} "DT_NODELABEL|${DT_LABEL_NODELABEL}")
  if(${${var}} STREQUAL ${var}-NOTFOUND)
    if(DT_LABEL_REQUIRED)
      message(FATAL_ERROR "required nodelabel not found: ${DT_LABEL_NODELABEL}")
    endif()
    set(${var})
  endif()

  set(${var} ${${var}} PARENT_SCOPE)
endfunction()

function(dt_alias var)
  set(options "REQUIRED")
  set(req_single_args "PROPERTY")
  set(single_args "TARGET")
  cmake_parse_arguments(DT_ALIAS "${options}" "${req_single_args};${single_args}" "" ${ARGN})

  dt_target_internal(DT_ALIAS_TARGET)

  if(${ARGV0} IN_LIST req_single_args)
    message(FATAL_ERROR "dt_alias(${ARGV0} ...) missing return parameter.")
  endif()

  foreach(arg ${req_single_args})
    if(NOT DEFINED DT_ALIAS_${arg})
      message(FATAL_ERROR "dt_alias(${ARGV0} ...) "
                          "missing required argument: ${arg}"
      )
    endif()
  endforeach()

  get_target_property(${var} ${DT_ALIAS_TARGET} "DT_ALIAS|${DT_ALIAS_PROPERTY}")
  if(${${var}} STREQUAL ${var}-NOTFOUND)
    if(DT_ALIAS_REQUIRED)
      message(FATAL_ERROR "required alias not found: ${DT_ALIAS_PROPERTY}")
    endif()
    set(${var})
  endif()

  set(${var} ${${var}} PARENT_SCOPE)
endfunction()

function(dt_prop var)
  set(options "REQUIRED")
  set(req_single_args "PATH;PROPERTY")
  set(single_args "INDEX;TARGET")
  cmake_parse_arguments(DT_PROP "${options}" "${req_single_args};${single_args}" "" ${ARGN})

  dt_target_internal(DT_PROP_TARGET)

  if(${ARGV0} IN_LIST req_single_args)
    message(FATAL_ERROR "dt_prop(${ARGV0} ...) missing return parameter.")
  endif()

  foreach(arg ${req_single_args})
    if(NOT DEFINED DT_PROP_${arg})
      message(FATAL_ERROR "dt_prop(${ARGV0} ...) "
                          "missing required argument: ${arg}"
      )
    endif()
  endforeach()

  dt_path_internal(canonical "${DT_PROP_PATH}" "${DT_PROP_TARGET}")
  get_property(exists TARGET ${DT_PROP_TARGET}
      PROPERTY "DT_PROP|${canonical}|${DT_PROP_PROPERTY}"
      SET
  )

  if(NOT exists)
    set(${var} PARENT_SCOPE)
    if(DT_PROP_REQUIRED)
      message(FATAL_ERROR "required property not found: ${canonical}/${DT_PROP_PROPERTY}")
    endif()
    return()
  endif()

  get_target_property(val ${DT_PROP_TARGET}
      "DT_PROP|${canonical}|${DT_PROP_PROPERTY}"
  )

  if(DEFINED DT_PROP_INDEX)
    list(GET val ${DT_PROP_INDEX} element)
    set(${var} "${element}" PARENT_SCOPE)
  else()
    set(${var} "${val}" PARENT_SCOPE)
  endif()
endfunction()

function(dt_target_internal var)
  if(NOT DEFINED ${var})
    set(${var} devicetree_target)
    set(${var} ${${var}} PARENT_SCOPE)
  endif()

  set(devicetree_imported FALSE)
  if(TARGET ${${var}})
    dt_path_internal_exists(devicetree_imported "/" ${${var}})
  endif()
  if(NOT devicetree_imported)
    message(FATAL_ERROR "devicetree is not yet imported into target '${${var}}'")
  endif()
endfunction()

function(dt_path_internal var path target)
  string(FIND "${path}" "/" slash_index)

  if("${slash_index}" EQUAL 0)
    # If the string starts with a slash, it should be an existing
    # canonical path.
    dt_path_internal_exists(check "${path}" "${target}")
    if (check)
      set(${var} "${path}" PARENT_SCOPE)
      return()
    endif()
  else()
    # Otherwise, try to expand a leading alias.
    string(SUBSTRING "${path}" 0 "${slash_index}" alias_name)
    dt_alias(alias_path PROPERTY "${alias_name}" TARGET "${target}")

    # If there is a leading alias, append the rest of the string
    # onto it and see if that's an existing node.
    if (DEFINED alias_path)
      set(rest)
      if (NOT "${slash_index}" EQUAL -1)
        string(SUBSTRING "${path}" "${slash_index}" -1 rest)
      endif()
      dt_path_internal_exists(expanded_path_exists "${alias_path}${rest}" "${target}")
      if (expanded_path_exists)
        set(${var} "${alias_path}${rest}" PARENT_SCOPE)
        return()
      endif()
    endif()
  endif()

  # Failed search; ensure return variable is undefined.
  set(${var} PARENT_SCOPE)
endfunction()

function(dt_path_internal_exists var path target)
  get_target_property(path_prop "${target}" "DT_NODE|${path}")
  if (path_prop)
    set(${var} TRUE PARENT_SCOPE)
  else()
    set(${var} FALSE PARENT_SCOPE)
  endif()
endfunction()

function(dt_arch_type var index)
  dt_nodelabel(cpu_path NODELABEL "cpu${index}")
  dt_prop(arch_type PATH "${cpu_path}" PROPERTY "compatible")
  set(${var} ${arch_type} PARENT_SCOPE)
endfunction()

function(dt_get_soc_name var)
  dt_prop(soc_name PATH "/" PROPERTY "compatible")
  set(${var} ${soc_name} PARENT_SCOPE)
endfunction()

function(zephyr_dt_preprocess)
  set(req_single_args "OUT_FILE")
  set(single_args "DEPS_FILE;WORKING_DIRECTORY")
  set(req_multi_args "CPP;SOURCE_FILES")
  set(multi_args "EXTRA_CPPFLAGS;INCLUDE_DIRECTORIES")
  cmake_parse_arguments(DT_PREPROCESS "" "${req_single_args};${single_args}" "${req_multi_args};${multi_args}" ${ARGN})

  foreach(arg ${req_single_args} ${req_multi_args})
    if(NOT DEFINED DT_PREPROCESS_${arg})
      message(FATAL_ERROR "dt_preprocess() missing required argument: ${arg}")
    endif()
  endforeach()

  set(include_opts)
  foreach(dir ${DT_PREPROCESS_INCLUDE_DIRECTORIES})
    list(APPEND include_opts -isystem ${dir})
  endforeach()

  set(source_opts)
  foreach(file ${DT_PREPROCESS_SOURCE_FILES})
    list(APPEND source_opts -include ${file})
  endforeach()

  set(deps_opts)
  if(DEFINED DT_PREPROCESS_DEPS_FILE)
    list(APPEND deps_opts -MD -MF ${DT_PREPROCESS_DEPS_FILE})
  endif()

  set(workdir_opts)
  if(DEFINED DT_PREPROCESS_WORKING_DIRECTORY)
    list(APPEND workdir_opts WORKING_DIRECTORY ${DT_PREPROCESS_WORKING_DIRECTORY})
  endif()

  # We are leaving linemarker directives enabled on purpose. This tells
  # dtlib where each line actually came from, which improves error
  # reporting.
  set(preprocess_cmd ${DT_PREPROCESS_CPP}
    -x assembler-with-cpp
    -nostdinc
    ${include_opts}
    ${source_opts}
    ${NOSYSDEF_CFLAG}
    -D__DTS__
    ${DT_PREPROCESS_EXTRA_CPPFLAGS}
    -E   # Stop after preprocessing
    ${deps_opts}
    -o ${DT_PREPROCESS_OUT_FILE}
    ${NULL_C}
    ${workdir_opts})

  execute_process(COMMAND ${preprocess_cmd} RESULT_VARIABLE ret)
  if(NOT "${ret}" STREQUAL "0")
    message(FATAL_ERROR "failed to preprocess devicetree files (error code ${ret}): ${DT_PREPROCESS_SOURCE_FILES}")
  endif()
endfunction()

function(zephyr_dt_import)
  set(req_single_args "EDT_PICKLE_FILE;TARGET")
  cmake_parse_arguments(arg "" "${req_single_args}" "" ${ARGN})
  zephyr_check_arguments_required_all(${CMAKE_CURRENT_FUNCTION} arg ${req_single_args})

  set(gen_dts_cmake_script ${TOOLS_DIR}/scripts/dts/gen_dts_cmake.py)
  set(gen_dts_cmake_output ${arg_EDT_PICKLE_FILE}.cmake)

  if((${arg_EDT_PICKLE_FILE} IS_NEWER_THAN ${gen_dts_cmake_output}) OR
     (${gen_dts_cmake_script} IS_NEWER_THAN ${gen_dts_cmake_output})
  )
    execute_process(
      COMMAND ${PYTHON_EXECUTABLE} ${gen_dts_cmake_script}
      --edt-pickle ${arg_EDT_PICKLE_FILE}
      --cmake-out ${gen_dts_cmake_output}
      WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
      RESULT_VARIABLE ret
      COMMAND_ERROR_IS_FATAL ANY
    )
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${gen_dts_cmake_script})

  set(DEVICETREE_TARGET ${arg_TARGET})
  include(${gen_dts_cmake_output})
endfunction()

macro(zephyr_check_arguments_required_all function prefix)
  foreach(required ${ARGN})
    if(NOT DEFINED ${prefix}_${required})
      message(FATAL_ERROR "${function}(...) missing a required argument: ${required}")
    endif()
  endforeach()
endmacro()
if(CMAKE_SCRIPT_MODE_FILE)

  function(add_custom_target)
    # This silence the error: 'add_custom_target command is not scriptable'
  endfunction()

  function(set_target_properties)
    # This silence the error: 'set_target_properties command is not scriptable'
  endfunction()
endif()
