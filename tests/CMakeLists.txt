cmake_minimum_required(VERSION 3.20)
project(rtos_sdk_host_tests LANGUAGES CXX)

enable_testing()

add_compile_options(
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<CXX_COMPILER_ID:MSVC>:/WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Werror>)

add_executable(link_fragment_test
    ../component/link/tests/fragment_test.cc)
target_compile_features(link_fragment_test PRIVATE cxx_std_17)
target_include_directories(link_fragment_test PRIVATE
    ../component/link/include)
add_test(NAME link_fragment COMMAND link_fragment_test)

add_executable(link_codec_test
    ../component/link/tests/codec_test.cc
    ../component/link/src/codec.cc
    ../component/link/src/crc16.cc)
target_compile_features(link_codec_test PRIVATE cxx_std_17)
target_include_directories(link_codec_test PRIVATE
    ../component/link/include)
add_test(NAME link_codec COMMAND link_codec_test)

add_executable(link_replay_cache_test
    ../component/link/tests/replay_cache_test.cc
    ../component/link/src/crc16.cc)
target_compile_features(link_replay_cache_test PRIVATE cxx_std_17)
target_include_directories(link_replay_cache_test PRIVATE
    ../component/link/include)
add_test(NAME link_replay_cache COMMAND link_replay_cache_test)

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

add_executable(gimbal_core_test
    ../component/gimbal/tests/gimbal_core_test.cc
    ../component/gimbal/src/attitude_ekf.cc
    ../component/gimbal/src/controller.cc
    ../component/gimbal/src/dynamics.cc
    ../component/gimbal/src/hall_calibration.cc
    ../component/gimbal/src/hall_sensor.cc
    ../component/gimbal/src/kinematics.cc
    ../component/gimbal/src/math.cc
    ../component/gimbal/src/motion_planner.cc
    ../component/gimbal/src/parameters.cc
    ../component/gimbal/src/safety_manager.cc
    ../component/gimbal/src/thermal_controller.cc
    ../component/gimbal/src/voltage_foc.cc)
target_compile_features(gimbal_core_test PRIVATE cxx_std_17)
target_include_directories(gimbal_core_test PRIVATE
    stubs
    ../component/gimbal/include
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(gimbal_core_test PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)
add_test(NAME gimbal_core COMMAND gimbal_core_test)

add_library(driver_header_contract OBJECT
    driver_header_contract.cc)
target_compile_features(driver_header_contract PRIVATE cxx_std_17)
target_include_directories(driver_header_contract PRIVATE
    stubs
    ../embedded/include
    ../embedded/include/osal
    ../embedded/osal/baremetal)
target_compile_definitions(driver_header_contract PRIVATE
    CONFIG_NUM_IRQ_PRIO_BITS=4)
