sdk_add_library(app ${NULL_C})
target_include_directories(app PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_sources(app PRIVATE
    main.cc
    services/app_services.cc
    board/board_devices.cc
    control/motor_control.cc
    control/motor_management.cc
    control/interaction_control.cc
    control/calibration_store.cc
    control/control_app.cc
    comm/motor_protocol.cc
    comm/interaction_protocol.cc
    comm/joint_canopen_fd.cc
    comm/motor_link.cc
    ${DRIVERS_GENERATED_CC}
    ${PINCTRL_GENERATED_CC})
target_link_libraries(app PRIVATE boot_common drivers osal sdk::motor nvs link)
function(check_demo_foc_callbacks)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${PYTHON_EXECUTABLE} ${TOP_DIR}/tools/demo_foc/check_elf.py
            $<TARGET_FILE:${PROJECT_NAME}> --nm ${CMAKE_NM}
        COMMENT "Checking motor control callback retention" VERBATIM)
endfunction()
cmake_language(DEFER DIRECTORY "${TOP_DIR}" CALL check_demo_foc_callbacks)
# Product-local LTO removes unused profile construction and folds the fixed
# single-axis coordinator without changing the image/storage partition layout.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    foreach(control_target app algo motor link nvs boot_common system system_fault osal
            drivers gpio can flash pwm adc pinctrl interrupt_controller clock_control log uart dma)
        if(NOT TARGET ${control_target})
            continue()
        endif()
        set_property(TARGET ${control_target} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        get_target_property(control_sources ${control_target} SOURCES)
        get_target_property(control_source_dir ${control_target} SOURCE_DIR)
        foreach(control_source IN LISTS control_sources)
            get_filename_component(control_source "${control_source}" ABSOLUTE BASE_DIR "${control_source_dir}")
            set_property(SOURCE "${control_source}" TARGET_DIRECTORY ${control_target}
                APPEND PROPERTY COMPILE_OPTIONS -Os)
        endforeach()
    endforeach()
    target_link_options(app INTERFACE -flto -Os -ffunction-sections -fdata-sections)
endif()
# Keep protocol/management metadata compact for non-GNU builds too.
set_property(SOURCE comm/motor_protocol.cc control/motor_management.cc control/control_app.cc
    APPEND PROPERTY COMPILE_OPTIONS
    "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang>:-Os>")
