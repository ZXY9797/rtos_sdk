"""Exercise the real DT -> Kconfig -> C++ integration with an ARM toolchain."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]
ARM = os.environ.get('ARMGCC9_TOOLCHAIN_PATH') or shutil.which('arm-none-eabi-gcc')


@unittest.skipUnless(ARM and shutil.which('cmake') and shutil.which('ninja'), 'ARM CMake build required')
class ImageDeviceTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.build = self.root / 'build'

    def configure(self, *options, error=None):
        result = subprocess.run(['cmake', '-S', str(ROOT), '-B', str(self.build),
                                 '-G', 'Ninja', '-Dp=demo_foc', *options],
                                capture_output=True, text=True, errors='replace')
        output = result.stdout + result.stderr
        if error is None:
            self.assertEqual(result.returncode, 0, output)
        else:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(error, " ".join(output.split()))

    def test_generated_configuration_has_one_source_per_setting(self):
        self.configure()
        config = (self.build / '.config').read_bytes()
        cache = self.build / '.cmake.dotconfig.checksum'
        checksum = cache.read_bytes()
        header = self.build / 'include/generated/drivers_generated.h'
        header.unlink()
        subprocess.run(['cmake', '--build', str(self.build), '--target', 'device_codegen'],
                       check=True, capture_output=True)
        self.assertTrue(header.exists())
        self.assertEqual(cache.read_bytes(), checksum)
        explicit = self.root / 'manual.conf'
        explicit.write_text(''.join(f'CONFIG_{family}_GD32=y\n'
                                    for family in ('ADC', 'CAN', 'DMA', 'FLASH', 'PWM', 'UART')))
        self.configure('-DEXTRA_CONF_FILE=' + explicit.as_posix())
        self.assertEqual((self.build / '.config').read_bytes(), config)
        self.assertNotIn(b'CONFIG_ADC_INIT_PRIORITY', config)
        explicit.write_text('CONFIG_ADC_INIT_PRIORITY=77\n')
        self.configure(error='ADC_INIT_PRIORITY')
        overlay = self.root / 'priority.overlay'
        overlay.write_text('&adc0 { init-priority = <77>; };\n')
        self.configure('-UEXTRA_CONF_FILE', '-DDTC_OVERLAY_FILE=' + overlay.as_posix())
        generated = self.build / 'include/generated'
        devices = json.loads((generated / 'devices.json').read_text())['devices']
        adc = next(item for item in devices if item['alias'] == 'adc_current')
        self.assertEqual(adc['init_priority'], 77)
        self.assertIn('triggered-scan', adc['capabilities'])
        self.assertRegex((generated / 'drivers_generated.cc').read_text(),
                         r'SYS_INIT_ROLLBACK\(hal::_init_adc_current,[^;]*, 77\)')

    def test_changed_hardware_rechecks_unchanged_handwritten_config(self):
        extra = self.root / 'backend.conf'
        extra.write_text('CONFIG_CAN_GD32=y\n')
        overlay = self.root / 'hardware.overlay'
        overlay.write_text('&can0 { status = "okay"; };\n')
        self.configure('-DAPPLICATION=samples/osal_contract',
                       '-DEXTRA_CONF_FILE=' + extra.as_posix(),
                       '-DDTC_OVERLAY_FILE=' + overlay.as_posix())
        overlay.write_text('&can0 { status = "disabled"; };\n')
        self.configure(error="was assigned the value 'y' but got the value 'n'")

    def test_missing_backend_and_fd_mode_fail_at_configure(self):
        extra = self.root / 'can.conf'
        for family in ('y', 'n'):
            extra.write_text(f'CONFIG_CAN={family}\nCONFIG_CAN_GD32=n\n')
            self.configure('-DEXTRA_CONF_FILE=' + extra.as_posix(),
                           error='enabled device requires CONFIG_CAN_GD32=y')
        overlay = self.root / 'classic.overlay'
        overlay.write_text('&can0 { /delete-property/ fd-enabled; };\n')
        self.configure('-UEXTRA_CONF_FILE', '-DDTC_OVERLAY_FILE=' + overlay.as_posix(),
                       error='can0: image requires an enabled device with can-fd')

    def test_board_identity_ignores_image_overlay_and_tracks_included_pins(self):
        config = self.root / 'board-copy'
        shutil.copytree(ROOT / 'app/product/demo_foc/config', config)
        self.configure('-DCONFIG_DIR=' + config.as_posix())
        def identity():
            info = json.loads((self.build / 'build-info.json').read_text(encoding='utf-8'))
            return info['inputs']['board_sha256'], info['inputs']['devicetree']

        original_board, original_image = identity()
        overlay = config / 'app.overlay'
        with overlay.open('a', encoding='utf-8') as file:
            file.write('\n/ { model = "overlay only"; };\n')
        self.configure()
        board, image = identity()
        self.assertEqual(board, original_board)
        self.assertNotEqual(image, original_image)
        pins = config / 'pinctrl.dtsi'
        content = pins.read_text(encoding='utf-8')
        old_pin = 'GD32_PINMUX_OUTPUT_LOW(GD32_PORT_B, 8,'
        self.assertIn(old_pin, content)
        pins.write_text(content.replace(old_pin, 'GD32_PINMUX_OUTPUT_LOW(GD32_PORT_B, 9,'),
                        encoding='utf-8')
        subprocess.run(['cmake', '--build', str(self.build), '--target', 'build_metadata'],
                       check=True, capture_output=True)
        self.assertNotEqual(identity()[0], original_board)

    def test_native_board_and_overlay_inputs_trigger_incremental_configure(self):
        config = self.root / 'board with native inputs'
        shutil.copytree(ROOT / 'app/product/demo_foc/config', config)
        loader = ROOT / 'app/product/demo_foc/boot/loader/config'
        shutil.copy(loader / 'loader.overlay', config)
        with (config / 'board.dts').open('a', encoding='utf-8') as board:
            board.write('\n/include/ "wiring.dtsi"\n')
        (config / 'wiring.dtsi').write_text('/include/ "pin.dtsi"\n')
        pin = config / 'pin.dtsi'
        pin_definition = '/ { /omit-if-no-ref/ extra_pin: extra-pin { pins = <%d>; }; };\n'
        pin.write_text(pin_definition % 1)
        with (config / 'app.overlay').open('a', encoding='utf-8') as overlay:
            overlay.write('\n/include/ "image-only.dtsi"\n')
        (config / 'image-only.dtsi').write_text('/ { sdk-data = /incbin/ ("image.bin"); };\n')
        binary = config / 'image.bin'
        binary.write_bytes(b'A')

        def identity(directory):
            inputs = json.loads((directory / 'build-info.json').read_text(encoding='utf-8'))['inputs']
            return inputs['board_sha256'], inputs['devicetree']

        def regenerate(directory):
            result = subprocess.run(['cmake', '--build', str(directory), '--target', 'device_codegen'],
                                    capture_output=True, text=True, errors='replace')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

        app_build = self.build
        self.configure('-DCONFIG_DIR=' + config.as_posix())
        self.build = boot_build = self.root / 'loader'
        self.configure('-DCONFIG_DIR=' + config.as_posix(), '-DFIRMWARE_TYPE=loader',
                       '-DCONF_FILE=' + (loader / 'prj.conf').as_posix())
        original_board, original_app = identity(app_build)
        self.assertEqual(identity(boot_build)[0], original_board)

        pin.write_text(pin_definition % 2)
        for build in (app_build, boot_build):
            regenerate(build)
        changed_board, image = identity(app_build)
        self.assertNotEqual(changed_board, original_board)
        self.assertEqual(image, original_app)  # The pin is unused in this image.
        self.assertEqual(identity(boot_build)[0], changed_board)
        binary.write_bytes(b'B')
        regenerate(app_build)
        board, image = identity(app_build)
        self.assertEqual(board, changed_board)
        self.assertNotEqual(image, original_app)
