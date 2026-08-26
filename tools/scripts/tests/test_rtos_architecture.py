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
        self.assertIn("gpio0: gpio@40010000", board)
        self.assertIn("reg = <0x40010000 0x400>", board)


if __name__ == "__main__":
    unittest.main()
