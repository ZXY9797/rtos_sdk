# Zephyr CMake Extensions
# This module provides Zephyr-specific CMake extensions

# Include guard
include_guard(GLOBAL)

# Zephyr compiler options function
function(zephyr_cc_option)
    foreach(option ${ARGN})
        target_compile_options(zephyr_interface INTERFACE
            $<$<COMPILE_LANGUAGE:C>:${option}>
        )
    endforeach()
endfunction()

# Zephyr compile options function
function(zephyr_compile_options)
    foreach(option ${ARGN})
        target_compile_options(zephyr_interface INTERFACE ${option})
    endforeach()
endfunction()

# Zephyr compile definitions function
function(zephyr_compile_definitions)
    foreach(def ${ARGN})
        target_compile_definitions(zephyr_interface INTERFACE ${def})
    endforeach()
endfunction()

# Zephyr include directories function
function(zephyr_include_directories)
    foreach(dir ${ARGN})
        target_include_directories(zephyr_interface INTERFACE ${dir})
    endforeach()
endfunction()

# Zephyr link libraries function
function(zephyr_link_libraries)
    foreach(lib ${ARGN})
        target_link_libraries(zephyr_interface INTERFACE ${lib})
    endforeach()
endfunction()

# Zephyr library named function
function(zephyr_library_named name)
    add_library(${name} STATIC)
    target_link_libraries(${name} PUBLIC zephyr_interface)
    set_property(GLOBAL APPEND PROPERTY ZEPHYR_LIBS ${name})
endfunction()

# Zephyr library sources function
function(zephyr_library_sources)
    foreach(source ${ARGN})
        target_sources(zephyr_library PRIVATE ${source})
    endforeach()
endfunction()

# Zephyr library sources if def
function(zephyr_library_sources_ifdef config)
    if(${${config}})
        zephyr_library_sources(${ARGN})
    endif()
endfunction()

# Zephyr compile options if def
function(zephyr_cc_option_ifdef config)
    if(${${config}})
        zephyr_cc_option(${ARGN})
    endif()
endfunction()

# Zephyr link libraries if def
function(zephyr_link_libraries_ifdef config)
    if(${${config}})
        zephyr_link_libraries(${ARGN})
    endif()
endfunction()

# Zephyr link libraries ifndef
function(zephyr_link_libraries_ifndef config)
    if(NOT ${${config}})
        zephyr_link_libraries(${ARGN})
    endif()
endfunction()

# Zephyr get function
function(zephyr_get var)
    set(options SYSBUILD LOCAL)
    set(oneValueArgs)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    if(DEFINED ${var})
        set(${var} ${${var}} PARENT_SCOPE)
    elseif(DEFINED CACHE{${var}})
        set(${var} $CACHE{${var}} PARENT_SCOPE)
    endif()
endfunction()

# Zephyr string function
function(zephyr_string)
    set(options)
    set(oneValueArgs SANITIZE TOUPPER)
    set(multiValueArgs)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    if(ARG_SANITIZE)
        string(MAKE_C_IDENTIFIER ${ARG_SANITIZE} result)
        set(${ARG_SANITIZE} ${result} PARENT_SCOPE)
    endif()
    
    if(ARG_TOUPPER)
        string(TOUPPER ${ARG_TOUPPER} result)
        set(${ARG_TOUPPER} ${result} PARENT_SCOPE)
    endif()
endfunction()

# Zephyr file copy function
function(zephyr_file_copy source dest)
    set(options ONLY_IF_DIFFERENT)
    cmake_parse_arguments(ARG "${options}" "" "" ${ARGN})
    
    if(ARG_ONLY_IF_DIFFERENT)
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy_if_different ${source} ${dest}
        )
    else()
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E copy ${source} ${dest}
        )
    endif()
endfunction()

# Import kconfig function
function(import_kconfig prefix kconfig_file)
    if(NOT EXISTS ${kconfig_file})
        message(WARNING "Kconfig file not found: ${kconfig_file}")
        return()
    endif()
    
    file(STRINGS ${kconfig_file} kconfig_lines)
    
    foreach(line ${kconfig_lines})
        # Match CONFIG_XXX=value
        if(line MATCHES "^(${prefix}_[A-Za-z0-9_]+)=(.*)$")
            set(key "${CMAKE_MATCH_1}")
            set(value "${CMAKE_MATCH_2}")
            
            # Remove quotes if present
            if(value MATCHES "^\"(.*)\"$")
                set(value "${CMAKE_MATCH_1}")
            endif()
            
            # Set the variable
            set(${key} "${value}" PARENT_SCOPE)
            
            # Also set as cache variable
            set(${key} "${value}" CACHE STRING "" FORCE)
        endif()
        
        # Match # CONFIG_XXX is not set
        if(line MATCHES "^# (${prefix}_[A-Za-z0-9_]+) is not set$")
            set(key "${CMAKE_MATCH_1}")
            set(${key} "" PARENT_SCOPE)
            set(${key} "" CACHE STRING "" FORCE)
        endif()
    endforeach()
endfunction()

# Property base for linker
add_library PROPERTY INTERFACE)

# Linker properties
set_property(TARGET PROPERTY PROPERTY no_relax "-Wl,--no-relax")
set_property(TARGET PROPERTY PROPERTY relax "-Wl,--relax")
set_property(TARGET PROPERTY PROPERTY sort_alignment "-Wl,--sort-section=alignment")
set_property(TARGET PROPERTY PROPERTY orphan_warning "-Wl,--orphan-handling=warn")
set_property(TARGET PROPERTY PROPERTY orphan_error "-Wl,--orphan-handling=error")
set_property(TARGET PROPERTY PROPERTY no_position_independent "-fno-pic -fno-pie")
set_property(TARGET PROPERTY PROPERTY baremetal "-nostartfiles -nostdlib -nodefaultlibs")

# Compiler properties
set_property(TARGET compiler PROPERTY no_strict_aliasing "-fno-strict-aliasing")
set_property(TARGET compiler PROPERTY no_common "-fno-common")
set_property(TARGET compiler PROPERTY debug "-g")
set_property(TARGET compiler PROPERTY freestanding "-ffreestanding")
set_property(TARGET compiler PROPERTY no_position_independent "-fno-pic -fno-pie")
set_property(TARGET compiler PROPERTY no_track_macro_expansion "-fno-track-macro-expansion")

# Optimization properties
set_property(TARGET compiler PROPERTY no_optimization "-O0")
set_property(TARGET compiler PROPERTY optimization_debug "-Og")
set_property(TARGET compiler PROPERTY optimization_speed "-O2")
set_property(TARGET compiler PROPERTY optimization_size "-Os")
set_property(TARGET compiler PROPERTY optimization_size_aggressive "-Oz")

# ASM properties
set_property(TARGET asm PROPERTY no_optimization "-O0")
set_property(TARGET asm PROPERTY optimization_debug "-Og")
set_property(TARGET asm PROPERTY optimization_speed "-O2")
set_property(TARGET asm PROPERTY optimization_size "-Os")
set_property(TARGET asm PROPERTY optimization_size_aggressive "-Oz")

# Linker properties
set_property(TARGET linker PROPERTY no_optimization "")
set_property(TARGET linker PROPERTY optimization_debug "")
set_property(TARGET linker PROPERTY optimization_speed "")
set_property(TARGET linker PROPERTY optimization_size "")
set_property(TARGET linker PROPERTY optimization_size_aggressive "")

# Compiler warning properties
set_property(TARGET compiler PROPERTY warning_base "-Wall -Wformat -Wformat-security -Wno-format-zero-length -Wno-main-return-type -Wno-address-of-packed-member -Wno-unused-but-set-variable -Wno-stringop-truncation")
set_property(TARGET compiler PROPERTY warning_extended "")
set_property(TARGET compiler PROPERTY warning_dw_1 "")
set_property(TARGET compiler PROPERTY warning_dw_2 "")
set_property(TARGET compiler PROPERTY warning_dw_3 "")

# Security properties
set_property(TARGET compiler PROPERTY security_canaries "-fstack-protector")
set_property(TARGET compiler PROPERTY security_canaries_strong "-fstack-protector-strong")
set_property(TARGET compiler PROPERTY security_canaries_all "-fstack-protector-all")
set_property(TARGET compiler PROPERTY security_canaries_explicit "")
set_property(TARGET compiler PROPERTY security_fortify_run_time "-D_FORTIFY_SOURCE=2")
set_property(TARGET compiler PROPERTY security_fortify_compile_time "-D_FORTIFY_SOURCE=1")
set_property(TARGET compiler PROPERTY security_undefined "-fsanitize=undefined")

# Imacros property
set_property(TARGET compiler PROPERTY imacros "-imacros")

# LTO property
set_property(TARGET compiler PROPERTY optimization_lto "-flto")
set_property(TARGET linker PROPERTY lto_arguments "-flto")

# Warnings as errors
set_property(TARGET compiler PROPERTY warnings_as_errors "-Werror")
set_property(TARGET linker PROPERTY warnings_as_errors "-Wl,--fatal-warnings")

# C++ properties
set_property(TARGET compiler-cpp PROPERTY required "-fno-exceptions -fno-rtti")
set_property(TARGET compiler-cpp PROPERTY no_exceptions "-fno-exceptions")
set_property(TARGET compiler-cpp PROPERTY no_rtti "-fno-rtti")
set_property(TARGET compiler-cpp PROPERTY dialect_cpp98 "-std=c++98")
set_property(TARGET compiler-cpp PROPERTY dialect_cpp11 "-std=c++11")
set_property(TARGET compiler-cpp PROPERTY dialect_cpp14 "-std=c++14")
set_property(TARGET compiler-cpp PROPERTY dialect_cpp17 "-std=c++17")
set_property(TARGET compiler-cpp PROPERTY dialect_cpp20 "-std=c++20")
set_property(TARGET compiler-cpp PROPERTY dialect_cpp23 "-std=c++23")
