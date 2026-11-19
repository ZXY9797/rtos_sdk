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
    comm/motor_link.cc)
target_link_libraries(app PRIVATE boot_common drivers osal sdk::motor nvs link)
function(check_demo_foc_callbacks)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${PYTHON_EXECUTABLE} ${TOP_DIR}/tools/demo_foc/check_elf.py
            $<TARGET_FILE:${PROJECT_NAME}> --nm ${CMAKE_NM}
        COMMENT "Checking motor control callback retention" VERBATIM)
endfunction()
cmake_language(DEFER DIRECTORY "${TOP_DIR}" CALL check_demo_foc_callbacks)
