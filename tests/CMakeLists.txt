cmake_minimum_required(VERSION 3.20)
project(rtos_sdk_host_tests LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()
add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/WX>
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>)
add_subdirectory(../samples/control_pipeline control-pipeline)
add_subdirectory(../samples/interaction_control interaction-control)
add_executable(interaction_test interaction_test.cc)
target_link_libraries(interaction_test PRIVATE sdk::algo)
target_compile_options(interaction_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG> $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)
add_test(NAME interaction COMMAND interaction_test)

foreach(contract algorithm calibration control diagnostics)
    add_executable(motor_${contract}_test motor_${contract}_test.cc)
    target_link_libraries(motor_${contract}_test PRIVATE sdk::algo)
    target_compile_options(motor_${contract}_test PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)
    add_test(NAME motor_${contract} COMMAND motor_${contract}_test)
endforeach()

add_executable(demo_foc_control_test
    demo_foc_control_test.cc
    ../app/product/demo_foc/control/motor_control.cc
    ../app/product/demo_foc/control/motor_management.cc
    ../app/product/demo_foc/control/interaction_control.cc
    ../app/product/demo_foc/comm/motor_protocol.cc
    ../app/product/demo_foc/comm/interaction_protocol.cc)
target_include_directories(demo_foc_control_test PRIVATE
    ../app/product/demo_foc ../component/link/include)
target_link_libraries(demo_foc_control_test PRIVATE sdk::algo)
target_compile_options(demo_foc_control_test PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)
add_test(NAME demo_foc_control COMMAND demo_foc_control_test)

add_executable(link_can_observer_test link_can_observer_test.cc)
target_include_directories(link_can_observer_test PRIVATE
    stubs ../component/link/include ../embedded/include ../embedded/include/osal ../embedded/osal/baremetal)
target_compile_definitions(link_can_observer_test PRIVATE CONFIG_NUM_IRQ_PRIO_BITS=4)
add_test(NAME link_can_observer COMMAND link_can_observer_test)

add_executable(adc_scan_test adc_scan_test.cc
    ../embedded/drivers/adc/adc_gd32.cc
    ../embedded/drivers/adc/adc_scan_gd32.cc
    ../embedded/drivers/dma/dma_gd32.cc)
target_include_directories(adc_scan_test PRIVATE stubs/adc ../embedded/include
    ../embedded/soc/gd/gd32f50x/include)
target_compile_options(adc_scan_test PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/UNDEBUG>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-UNDEBUG>)
if(MSVC)
    target_compile_options(adc_scan_test PRIVATE /FI${CMAKE_CURRENT_SOURCE_DIR}/stubs/adc/gd32f50x.h)
else()
    target_compile_options(adc_scan_test PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/stubs/adc/gd32f50x.h)
endif()
add_test(NAME adc_scan COMMAND adc_scan_test)

find_package(Threads REQUIRED)
add_executable(event_group_runtime_test event_group_runtime_test.cc ../embedded/osal/event_group.cc)
target_include_directories(event_group_runtime_test PRIVATE
    ../embedded/include ../embedded/include/osal ../embedded/osal/baremetal)
target_compile_definitions(event_group_runtime_test PRIVATE CONFIG_NUM_IRQ_PRIO_BITS=4)
target_link_libraries(event_group_runtime_test PRIVATE Threads::Threads)
add_test(NAME event_group_runtime COMMAND event_group_runtime_test)
set_tests_properties(event_group_runtime PROPERTIES TIMEOUT 15)

add_executable(runtime_contract_test runtime_contract_test.cc)
target_include_directories(runtime_contract_test PRIVATE ../embedded/include ../component/ipc/include)
target_link_libraries(runtime_contract_test PRIVATE sdk::runtime sdk::ipc)
add_test(NAME runtime_contract COMMAND runtime_contract_test)

add_executable(system_diagnostics_test system_diagnostics_test.cc ../embedded/system/diagnostics.cc)
target_include_directories(system_diagnostics_test PRIVATE stubs ../embedded/include
    ../embedded/include/osal ../embedded/osal/baremetal ../bootloader/include)
target_compile_definitions(system_diagnostics_test PRIVATE CONFIG_NUM_IRQ_PRIO_BITS=4)
target_link_libraries(system_diagnostics_test PRIVATE sdk::runtime)
add_test(NAME system_diagnostics COMMAND system_diagnostics_test "${CMAKE_CURRENT_BINARY_DIR}/diagnostics.bin")

add_executable(event_bits_test event_bits_test.cc)
target_compile_features(event_bits_test PRIVATE cxx_std_17)
target_include_directories(event_bits_test PRIVATE ../embedded/include)
add_test(NAME event_bits COMMAND event_bits_test)

add_executable(frame_tx_queue_test frame_tx_queue_test.cc)
target_compile_features(frame_tx_queue_test PRIVATE cxx_std_17)
target_include_directories(frame_tx_queue_test PRIVATE ../component/link/include)
add_test(NAME frame_tx_queue COMMAND frame_tx_queue_test)

add_executable(link_fragment_test
    ../component/link/tests/fragment_test.cc)
target_compile_features(link_fragment_test PRIVATE cxx_std_17)
target_include_directories(link_fragment_test PRIVATE
    ../component/link/include)
add_test(NAME link_fragment COMMAND link_fragment_test)

add_executable(link_codec_test
    ../component/link/tests/codec_test.cc
    ../component/link/src/codec.cc
    ../component/link/src/crc8.cc
    ../component/link/src/crc16.cc)
target_compile_features(link_codec_test PRIVATE cxx_std_17)
target_include_directories(link_codec_test PRIVATE
    ../component/link/include)
add_test(NAME link_codec COMMAND link_codec_test)

add_executable(link_codec_max_frame_test
    ../component/link/tests/codec_test.cc
    ../component/link/src/codec.cc
    ../component/link/src/crc8.cc
    ../component/link/src/crc16.cc)
target_compile_features(link_codec_max_frame_test PRIVATE cxx_std_17)
target_compile_definitions(link_codec_max_frame_test PRIVATE
    CONFIG_LINK_MAX_FRAME_SIZE=1023)
target_include_directories(link_codec_max_frame_test PRIVATE
    ../component/link/include)
add_test(NAME link_codec_max_frame COMMAND link_codec_max_frame_test)

add_executable(link_replay_cache_test
    ../component/link/tests/replay_cache_test.cc
    ../component/link/src/crc16.cc)
target_compile_features(link_replay_cache_test PRIVATE cxx_std_17)
target_include_directories(link_replay_cache_test PRIVATE
    ../component/link/include)
add_test(NAME link_replay_cache COMMAND link_replay_cache_test)

add_executable(link_diagnostics_test
    ../component/link/tests/diagnostics_test.cc
    ../component/link/src/diagnostics.cc)
target_compile_features(link_diagnostics_test PRIVATE cxx_std_17)
target_include_directories(link_diagnostics_test PRIVATE
    ../component/link/include)
add_test(NAME link_diagnostics COMMAND link_diagnostics_test)

add_executable(link_router_policy_test
    ../component/link/tests/router_policy_test.cc)
target_compile_features(link_router_policy_test PRIVATE cxx_std_17)
target_include_directories(link_router_policy_test PRIVATE
    stubs
    ../component/link/include
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(link_router_policy_test PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)
add_test(NAME link_router_policy COMMAND link_router_policy_test)

add_library(link_router_contract OBJECT
    ../component/link/src/router.cc)
target_compile_features(link_router_contract PRIVATE cxx_std_17)
target_include_directories(link_router_contract PRIVATE
    stubs
    ../component/link/include
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(link_router_contract PRIVATE
    CONFIG_LINK_PROCESS_HZ=1000
    CONFIG_LINK_PROCESS_STACK=2048
    CONFIG_LINK_PROCESS_PRIO=5
    CONFIG_NUM_IRQ_PRIO_BITS=4)

add_executable(can_dlc_test
    ../embedded/drivers/can/tests/can_dlc_test.cc)
target_compile_features(can_dlc_test PRIVATE cxx_std_17)
target_include_directories(can_dlc_test PRIVATE
    ../embedded/include)
add_test(NAME can_dlc COMMAND can_dlc_test)

add_executable(icm40609d_test
    ../embedded/drivers/imu/tests/icm40609d_test.cc)
target_compile_features(icm40609d_test PRIVATE cxx_std_17)
target_link_libraries(icm40609d_test PRIVATE sdk::runtime)
target_include_directories(icm40609d_test PRIVATE
    stubs
    ../embedded/drivers/imu/include
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(icm40609d_test PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)
add_test(NAME icm40609d COMMAND icm40609d_test)

add_executable(sensor_batch_core_test
    sensor_batch_core_test.cc
    ../embedded/system/sensor_core.cc)
target_compile_features(sensor_batch_core_test PRIVATE cxx_std_17)
target_link_libraries(sensor_batch_core_test PRIVATE sdk::runtime)
target_include_directories(sensor_batch_core_test PRIVATE
    stubs
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(sensor_batch_core_test PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)
add_test(NAME sensor_batch_core COMMAND sensor_batch_core_test)

add_executable(boot_power_loss_test
    ../bootloader/common/tests/boot_power_loss_test.cc
    ../bootloader/common/src/boot_ctrl.cc
    ../bootloader/loader/upgrade_logic/flash_copy.cc)
target_compile_features(boot_power_loss_test PRIVATE cxx_std_17)
target_include_directories(boot_power_loss_test PRIVATE
    ../bootloader/include)
add_test(NAME boot_power_loss COMMAND boot_power_loss_test)

add_executable(boot_protocol_policy_test
    ../bootloader/common/tests/boot_protocol_policy_test.cc)
target_compile_features(boot_protocol_policy_test PRIVATE cxx_std_17)
target_include_directories(boot_protocol_policy_test PRIVATE
    ../bootloader/include
    ../component/link/include)
add_test(NAME boot_protocol_policy COMMAND boot_protocol_policy_test)

add_executable(nvs_power_loss_test
    ../component/nvs/tests/nvs_power_loss_test.cc
    ../component/nvs/src/nvs.cc)
target_compile_features(nvs_power_loss_test PRIVATE cxx_std_17)
target_include_directories(nvs_power_loss_test PRIVATE
    ../component/nvs/include
    ../embedded/include
    ../embedded/osal/baremetal)
target_compile_definitions(nvs_power_loss_test PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)
add_test(NAME nvs_power_loss COMMAND nvs_power_loss_test)

add_library(boot_logic_contract OBJECT
    ../bootloader/loader/src/boot_logic.cc)
target_compile_features(boot_logic_contract PRIVATE cxx_std_17)
target_include_directories(boot_logic_contract PRIVATE
    stubs
    ../bootloader/include)
target_compile_definitions(boot_logic_contract PRIVATE
    CONFIG_BOOT_MODE_STAGED_COPY=1)

add_library(boot_upgrade_contract OBJECT
    ../bootloader/loader/upgrade_logic/upgrade_state.cc)
target_compile_features(boot_upgrade_contract PRIVATE cxx_std_17)
target_include_directories(boot_upgrade_contract PRIVATE
    stubs
    ../bootloader/include
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(boot_upgrade_contract PRIVATE
    CONFIG_BOOT_MODE_STAGED_COPY=1
    CONFIG_NUM_IRQ_PRIO_BITS=4)

add_library(device_pm_contract OBJECT
    ../embedded/system/device_pm.cc)
target_compile_features(device_pm_contract PRIVATE cxx_std_17)
target_include_directories(device_pm_contract PRIVATE
    stubs
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(device_pm_contract PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)

add_executable(device_pm_test
    device_pm_test.cc
    ../embedded/system/device_pm.cc)
target_compile_features(device_pm_test PRIVATE cxx_std_17)
target_include_directories(device_pm_test PRIVATE
    stubs
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(device_pm_test PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)
add_test(NAME device_pm COMMAND device_pm_test)

add_executable(gimbal_functional_test
    gimbal_functional_test.cc
    ../app/product/gimbal/services/parameters.cc
    ../component/algo/src/filter/biquad_filter.cc
    ../component/algo/src/filter/lead_lag_compensator.cc
    ../component/algo/src/trajectory/motion_profile.cc
    ../component/algo/src/math/spatial_math.cc
    ../component/attitude/src/attitude_ekf.cc
    ../component/control/src/controller.cc
    ../component/foc/src/svpwm.cc
    ../component/foc/src/voltage_foc.cc
    ../component/motion/src/dynamics.cc
    ../component/motion/src/kinematics.cc
    ../component/motion/src/motion_planner.cc
    ../component/position_sensor/src/hall_calibration.cc
    ../component/position_sensor/src/hall_sensor.cc
    ../component/safety/src/safety_manager.cc
    ../component/thermal/src/thermal_controller.cc)
target_compile_features(gimbal_functional_test PRIVATE cxx_std_17)
target_include_directories(gimbal_functional_test PRIVATE
    stubs
    ../app/product/gimbal/include
    ../component/algo/include
    ../component/attitude/include
    ../component/control/include
    ../component/control_contracts/include
    ../component/foc/include
    ../component/ipc/include
    ../component/motion/include
    ../component/position_sensor/include
    ../component/safety/include
    ../component/thermal/include
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(gimbal_functional_test PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)
add_test(NAME gimbal_functional COMMAND gimbal_functional_test)

add_library(driver_header_contract OBJECT
    driver_header_contract.cc)
target_compile_features(driver_header_contract PRIVATE cxx_std_17)
target_link_libraries(driver_header_contract PRIVATE sdk::runtime)
target_include_directories(driver_header_contract PRIVATE
    stubs
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(driver_header_contract PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)

# Handler archive retention is exercised by firmware builds.
add_executable(dma_memory_test dma_memory_test.cc)
target_compile_features(dma_memory_test PRIVATE cxx_std_17)
target_include_directories(dma_memory_test PRIVATE ../embedded/include)
add_test(NAME dma_memory COMMAND dma_memory_test)

foreach(assert_variant file no_file)
    add_executable(assert_abi_${assert_variant} assert_abi_test.cc assert_abi_backend.cc)
    target_include_directories(assert_abi_${assert_variant} PRIVATE stubs/assert_api ../embedded/include)
    target_compile_definitions(assert_abi_${assert_variant} PRIVATE
        CONFIG_ASSERT=1 CONFIG_ASSERT_LEVEL=2 CONFIG_ASSERT_TEST=1)
    if(assert_variant STREQUAL "no_file")
        target_compile_definitions(assert_abi_${assert_variant} PRIVATE CONFIG_ASSERT_NO_FILE_INFO=1)
    endif()
    add_test(NAME assert_abi_${assert_variant} COMMAND assert_abi_${assert_variant})
endforeach()
