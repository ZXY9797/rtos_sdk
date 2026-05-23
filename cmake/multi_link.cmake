# Multi-pass linking support for MSDK
# This module implements Zephyr's multi-pass linking mechanism

# Initialize linking pass variables
set(ZEPHYR_CURRENT_LINKER_PASS 0)
set(ZEPHYR_CURRENT_LINKER_CMD linker_zephyr_pre${ZEPHYR_CURRENT_LINKER_PASS}.cmd)
set(ZEPHYR_LINK_STAGE_EXECUTABLE zephyr_pre${ZEPHYR_CURRENT_LINKER_PASS})

# Set prebuilt executable based on configuration
if(CONFIG_USERSPACE OR CONFIG_DEVICE_DEPS)
    set(ZEPHYR_PREBUILT_EXECUTABLE zephyr_pre1)
else()
    set(ZEPHYR_PREBUILT_EXECUTABLE zephyr_pre0)
endif()
set(ZEPHYR_FINAL_EXECUTABLE zephyr_final)

# Function to generate linker script for a specific pass
function(generate_linker_script pass)
    set(options)
    set(oneValueArgs OUTPUT)
    set(multiValueArgs DEFINES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    # Set pass-specific defines
    set(PASS_DEFINES "")
    foreach(define ${ARG_DEFINES})
        list(APPEND PASS_DEFINES "-D${define}")
    endforeach()
    
    # Generate the linker script
    set(linker_script_output ${ARG_OUTPUT})
    
    # Use the ARM linker script generator
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/linker_script/arm/linker.cmake)
    
    message(STATUS "Generated linker script for pass ${pass}: ${linker_script_output}")
endfunction()

# Function to create a link target
function(create_link_target name)
    set(options)
    set(oneValueArgs LINKER_SCRIPT)
    set(multiValueArgs LIBRARIES SOURCES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} ${ARG_LIBRARIES})
    
    # Set linker script
    if(ARG_LINKER_SCRIPT)
        set_target_properties(${name} PROPERTIES
            LINK_DEPENDS ${ARG_LINKER_SCRIPT}
        )
    endif()
endfunction()

# Main multi-pass linking function
function(perform_multi_pass_linking)
    set(options)
    set(oneValueArgs PROJECT_NAME)
    set(multiValueArgs LIBRARIES)
    cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    
    set(project_name ${ARG_PROJECT_NAME})
    set(libraries ${ARG_LIBRARIES})
    
    # Pass 0: Initial linking
    message(STATUS "Performing pass 0 linking...")
    generate_linker_script(0
        OUTPUT ${PROJECT_BINARY_DIR}/linker_zephyr_pre0.cmd
        DEFINES "LINKER_PASS_0"
    )
    
    create_link_target(zephyr_pre0
        LINKER_SCRIPT ${PROJECT_BINARY_DIR}/linker_zephyr_pre0.cmd
        LIBRARIES ${libraries}
        SOURCES ${NULL_C}
    )
    
    # Pass 1: If userspace or device deps enabled
    if(CONFIG_USERSPACE OR CONFIG_DEVICE_DEPS)
        message(STATUS "Performing pass 1 linking...")
        
        # Run gen_device_deps.py if needed
        if(CONFIG_DEVICE_DEPS)
            add_custom_command(
                OUTPUT device_deps.c
                COMMAND ${PYTHON_EXECUTABLE}
                    ${ZEPHYR_BASE}/scripts/build/gen_device_deps.py
                    --output-source device_deps.c
                    --kernel $<TARGET_FILE:zephyr_pre0>
                DEPENDS zephyr_pre0
            )
        endif()
        
        # Run gen_kobject_list.py if needed
        if(CONFIG_USERSPACE)
            add_custom_command(
                OUTPUT kobject_hash.c
                COMMAND ${PYTHON_EXECUTABLE}
                    ${ZEPHYR_BASE}/scripts/build/gen_kobject_list.py
                    --kernel $<TARGET_FILE:zephyr_pre0>
                    --gperf-output kobject_hash.gperf
                DEPENDS zephyr_pre0
            )
        endif()
        
        generate_linker_script(1
            OUTPUT ${PROJECT_BINARY_DIR}/linker_zephyr_pre1.cmd
            DEFINES "LINKER_PASS_1" "LINKER_ZEPHYR_PREBUILT"
        )
        
        create_link_target(zephyr_pre1
            LINKER_SCRIPT ${PROJECT_BINARY_DIR}/linker_zephyr_pre1.cmd
            LIBRARIES ${libraries}
            SOURCES ${NULL_C}
        )
    endif()
    
    # Final pass
    message(STATUS "Performing final linking...")
    generate_linker_script(final
        OUTPUT ${PROJECT_BINARY_DIR}/linker.cmd
        DEFINES "LINKER_PASS_FINAL" "LINKER_ZEPHYR_FINAL"
    )
    
    create_link_target(${project_name}
        LINKER_SCRIPT ${PROJECT_BINARY_DIR}/linker.cmd
        LIBRARIES ${libraries}
        SOURCES ${NULL_C}
    )
    
    # Generate output files
    add_custom_command(
        TARGET ${project_name}
        POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -Obinary
            "${project_name}${CMAKE_OUTPUT_SUFFIX}"
            "${project_name}.bin"
        COMMAND ${CMAKE_OBJCOPY} -Oihex
            "${project_name}${CMAKE_OUTPUT_SUFFIX}"
            "${project_name}.hex"
        COMMAND ${CMAKE_SIZE} --format=berkeley
            "${project_name}${CMAKE_OUTPUT_SUFFIX}"
        COMMENT "Generating output files..."
    )
endfunction()
