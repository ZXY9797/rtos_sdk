# Linker script common CMake module
# This module provides functions for generating linker scripts

# Create a function to configure linker script
function(configure_linker_script output_file)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs DEFINES DEPENDS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    # Get the linker script template
    set(linker_script_template ${CMAKE_CURRENT_SOURCE_DIR}/cmake/linker_script/${ARCH}/linker.cmake)
    
    # Generate the linker script
    configure_file(${linker_script_template} ${output_file})
endfunction()

# Create a function to add linker script sources
function(zephyr_linker_sources location)
    set(options)
    set(oneValueArgs)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    # Store the sources for later use
    set_property(GLOBAL APPEND PROPERTY ZEPHYR_LINKER_SOURCES_${location} ${ARG_UNPARSED_ARGUMENTS})
endfunction()

# Create a function to include generated linker files
function(zephyr_linker_include_generated type file)
    set_property(GLOBAL APPEND PROPERTY ZEPHYR_LINKER_INCLUDE_${type} ${file})
endfunction()

# Create a function to define memory regions
function(zephyr_linker_memory)
    set(options)
    set(oneValueArgs NAME FLAGS START SIZE)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    set_property(GLOBAL APPEND PROPERTY ZEPHYR_LINKER_MEMORY_REGIONS
        "${ARG_NAME} ${ARG_FLAGS} ${ARG_START} ${ARG_SIZE}")
endfunction()

# Create a function to define sections
function(zephyr_linker_section)
    set(options NOINPUT)
    set(oneValueArgs NAME VMA LMA GROUP TYPE SUBALIGN ADDRESS)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    set_property(GLOBAL APPEND PROPERTY ZEPHYR_LINKER_SECTIONS
        "${ARG_NAME} ${ARG_VMA} ${ARG_LMA} ${ARG_GROUP}")
endfunction()

# Create a function to configure sections
function(zephyr_linker_section_configure)
    set(options KEEP)
    set(oneValueArgs SECTION INPUT ALIGN OFFSET PRIO SORT)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    set_property(GLOBAL APPEND PROPERTY ZEPHYR_LINKER_SECTION_CONFIGS
        "${ARG_SECTION} ${ARG_INPUT} ${ARG_ALIGN}")
endfunction()

# Create a function to define symbols
function(zephyr_linker_symbol)
    set(options)
    set(oneValueArgs SYMBOL EXPR)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    set_property(GLOBAL APPEND PROPERTY ZEPHYR_LINKER_SYMBOLS
        "${ARG_SYMBOL} ${ARG_EXPR}")
endfunction()
