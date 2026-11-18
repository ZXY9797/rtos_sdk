#!/usr/bin/env python3

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]


class RtosArchitectureContractTest(unittest.TestCase):
    def read(self, relative_path):
        return (REPO_ROOT / relative_path).read_text(encoding="utf-8")

    def test_goodix_rtos_app_does_not_poll_non_rtos_scheduler(self):
        product_sources = "\n".join(
            path.read_text(encoding="utf-8")
            for path in (REPO_ROOT / "app/product/demo_ble").rglob("*.cc")
        )
        self.assertNotIn("pwr_mgmt_schedule(", product_sources)

    def test_goodix_ble_software_irq_is_rtos_callable(self):
        stack = self.read("component/ble/goodix/src/ble_stack_impl.cc")
        self.assertIn(
            "NVIC_SetPriority(BLE_SDK_IRQn, osal::kLowestIrqPriority)",
            stack,
        )

    def test_linker_symbols_keep_global_c_linkage(self):
        startup = self.read("embedded/system/init.cc")
        c_linkage = startup.index('extern "C" {')
        unnamed_namespace = startup.index("namespace {")
        self.assertLess(c_linkage, unnamed_namespace)

    def test_disabled_ble_services_remove_wrappers_and_profiles(self):
        cmake = self.read("component/ble/goodix/CMakeLists.txt")
        for symbol, wrapper, profile in (
            ("CONFIG_BLE_HID", "src/ble_hid_impl.cc", "profiles/hids/hids.c"),
            ("CONFIG_BLE_UART_SERVICE", "src/ble_uart_impl.cc", "profiles/gus/gus.c"),
            ("CONFIG_BLE_BATTERY_SERVICE", "src/ble_batt_impl.cc", "profiles/bas/bas.c"),
            ("CONFIG_BLE_DEVICE_INFO", "src/ble_dis_impl.cc", "profiles/dis/dis.c"),
        ):
            block_start = cmake.index(f"if({symbol})")
            block_end = cmake.index("endif()", block_start)
            block = cmake[block_start:block_end]
            self.assertIn(wrapper, block)
            self.assertIn(profile, block)
        unconditional = cmake[:cmake.index("if(CONFIG_BLE_HID)")]
        self.assertNotIn("ble_hid_impl.cc", unconditional)
        self.assertNotIn("ble_uart_impl.cc", unconditional)
        self.assertNotIn("ble_batt_impl.cc", unconditional)
        self.assertNotIn("ble_dis_impl.cc", unconditional)

    def test_ble_configuration_has_no_unenforced_heap_contract(self):
        kconfig = self.read("component/ble/goodix/Kconfig")
        product_config = self.read("app/product/demo_ble/config/prj.conf")
        self.assertNotIn("BLE_HEAP_SIZE", kconfig)
        self.assertNotIn("BLE_HEAP_SIZE", product_config)

    def test_rtthread_uses_common_tick_and_stack_guard(self):
        config = self.read("embedded/osal/rt-thread/rtconfig.h")
        self.assertIn(
            "#define RT_TICK_PER_SECOND  CONFIG_SYS_CLOCK_TICKS_PER_SEC",
            config,
        )
        self.assertIn("#define RT_USING_OVERFLOW_CHECK", config)

    def test_demo_ble_gpio0_address_matches_goodix_soc(self):
        board = self.read("app/product/demo_ble/config/board.dts")
        overlay = self.read("app/product/demo_ble/config/app.overlay")
        self.assertIn('&gpio0', overlay)
        self.assertNotIn('current-speed', board)
        self.assertIn("gpio0: gpio@40010000", board)
        self.assertIn("reg = <0x40010000 0x400>", board)

    def test_products_use_sdk_components_without_internal_module_registration(self):
        component_cmake = self.read("component/CMakeLists.txt")
        self.assertFalse((REPO_ROOT / "app/modules/CMakeLists.txt").exists())
        self.assertIn("add_subdirectory(algo EXCLUDE_FROM_ALL)", component_cmake)
        self.assertIn("add_subdirectory(motor EXCLUDE_FROM_ALL)", component_cmake)
        self.assertIn("add_subdirectory_ifdef(CONFIG_FOC motor/drive)", component_cmake)
        self.assertIn("add_subdirectory(ipc)", component_cmake)
        for product in ("demo_foc", "gimbal"):
            self.assertIn("sdk::motor", self.read(f"app/product/{product}/CMakeLists.txt"))
        for sample in ("control_pipeline", "interaction_control"):
            self.assertNotIn("app/modules", self.read(f"samples/{sample}/CMakeLists.txt"))

    def test_gimbal_product_owns_policy_and_topic_topology(self):
        product_cmake = self.read("app/product/gimbal/CMakeLists.txt")
        product_kconfig = self.read("app/product/gimbal/Kconfig")
        domain_types = self.read(
            "app/product/gimbal/include/gimbal/types.h")
        self.assertIn("services/parameters.cc", product_cmake)
        self.assertIn("${CMAKE_CURRENT_SOURCE_DIR}/include", product_cmake)
        self.assertIn("select FOC_VOLTAGE_CONTROL", product_kconfig)
        self.assertNotIn("ProductMode", domain_types)

    def test_reusable_algorithms_do_not_depend_on_gimbal_or_platform(self):
        algo_cmake = self.read("component/algo/CMakeLists.txt")
        controller = self.read("app/product/gimbal/control/controller.cc")
        self.assertIn("src/math/spatial_math.cc", algo_cmake)
        self.assertIn("src/trajectory/motion_profile.cc", algo_cmake)
        self.assertIn("algo::BiquadFilter", self.read(
            "app/product/gimbal/include/gimbal/controller.h"))
        self.assertNotIn("FilterCoefficients", controller)
        platform_headers = r'(?:gimbal/|osal[./]|drivers/|device[./])'
        motor_headers = r'(?:foc/|algo/(?:(?:motor|calibration|interaction|robotics|foc)[./]|sensor/hall_))'
        for component, forbidden in (
            ("algo", (platform_headers, motor_headers)),
            ("motor", (platform_headers, r'foc/(?:config|motor|motor_device|voltage_motor)\.h')),
        ):
            root = REPO_ROOT / "component" / component
            for directory in (root / "include", root / "src"):
                for source in (*directory.rglob("*.h"), *directory.rglob("*.cc")):
                    with self.subTest(source=source.relative_to(REPO_ROOT)):
                        contents = source.read_text(encoding="utf-8")
                        for pattern in forbidden:
                            self.assertNotRegex(contents, rf'#\s*include\s*[<"]{pattern}')

    def test_foc_math_and_device_sources_have_separate_owners(self):
        motor_cmake = self.read("component/motor/CMakeLists.txt")
        drive_cmake = self.read("component/motor/drive/CMakeLists.txt")
        self.assertIn("REQUIRES sdk::algo", motor_cmake)
        self.assertIn("sdk::motor", drive_cmake)
        for source in ("svpwm.cc", "svpwm_sector.cc", "voltage_foc.cc"):
            self.assertIn(f"src/foc/{source}", motor_cmake)
            self.assertNotIn(source, drive_cmake)
        for mode, adapter in (("CURRENT", "motor.cc"), ("VOLTAGE", "voltage_motor.cc")):
            selector = f"target_sources_ifdef(CONFIG_FOC_{mode}_CONTROL"
            start = drive_cmake.index(selector)
            self.assertIn(f"src/{adapter}", drive_cmake[start:drive_cmake.index(")", start)])


if __name__ == "__main__":
    unittest.main()
