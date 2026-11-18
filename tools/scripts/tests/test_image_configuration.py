from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[3]


@unittest.skipUnless(shutil.which('cmake') and shutil.which('ninja'), 'CMake and Ninja required')
class ImageConfigurationTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix='sdk image config ')
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.source = self.root / 'source'
        self.source.mkdir()
        self.config = self.root / 'image config'
        self.config.mkdir()
        self.write(self.config / 'prj.conf', '# Base configuration\n')
        self.build = self.root / 'build'

    def write(self, path, text):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding='utf-8')

    def configure(self, image='app', custom=True, definitions=(), accept=True, sdk=ROOT):
        config = f'set(CONFIG_DIR "{self.config.as_posix()}")\n' if custom else ''
        text = ('cmake_minimum_required(VERSION 3.20)\nproject(selection LANGUAGES NONE)\n'
                f'set(SDK_ROOT "{sdk.as_posix()}")\nset(PROJECT_NAME demo_foc)\n'
                f'set(FIRMWARE_TYPE {image})\n' + config +
                f'include("{ROOT.as_posix()}/tools/cmake/env.cmake")\n'
                'file(WRITE "${PROJECT_BINARY_DIR}/selected.txt" '
                '"${PROJECT_CONFIG_DIR}\n${SDK_IMAGE_CONFIG_FILES}\n${SDK_IMAGE_OVERLAYS}\n'
                '${PROJECT_HARDWARE_DIR}\n${SDK_BOARD_DTS}\n")\n')
        self.write(self.source / 'CMakeLists.txt', text)
        result = subprocess.run(['cmake', '-S', str(self.source), '-B', str(self.build),
                                 '-G', 'Ninja', *definitions], capture_output=True, text=True)
        if accept:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return (self.build / 'selected.txt').read_text(encoding='utf-8').splitlines()
        self.assertNotEqual(result.returncode, 0)
        return result.stdout + result.stderr

    def test_default_app_and_boot_roots_are_separate(self):
        for image, expected in [('app', 'app/product/demo_foc/config'),
                                ('preloader', 'app/product/demo_foc/boot/preloader/config'),
                                ('loader', 'app/product/demo_foc/boot/loader/config'),
                                ('upgrade', 'app/product/demo_foc/boot/upgrade/config')]:
            with self.subTest(image=image):
                selected = self.configure(image, custom=False)
                self.assertEqual(selected[0], (ROOT / expected).as_posix())
                self.assertEqual(selected[2], (ROOT / expected / f'{image}.overlay').as_posix())

    def test_boot_profile_comes_from_its_image_config_directory(self):
        profile = self.config / 'profiles/contract.conf'
        self.write(profile, 'CONFIG_BOOT_PRODUCTION=y\n')
        selected = self.configure('loader', definitions=['-DPROFILE=contract'])
        self.assertEqual(selected[1].split(';'), [
            (ROOT / 'app/product/demo_foc/config/board_defconfig').as_posix(),
            (self.config / 'prj.conf').as_posix(), profile.as_posix()])

    def test_config_directory_override_keeps_product_hardware(self):
        selected = self.configure()
        self.assertEqual(selected[1].split(';')[0],
                         (ROOT / 'app/product/demo_foc/config/board_defconfig').as_posix())
        self.assertEqual(selected[3], (ROOT / 'app/product/demo_foc/config').as_posix())

    def test_project_config_hardware_is_shared_by_app_and_boot(self):
        sdk = self.root / 'isolated sdk'
        for name in ['app/product/demo_foc/CMakeLists.txt',
                     'bootloader/preloader/CMakeLists.txt',
                     'bootloader/loader/CMakeLists.txt',
                     'bootloader/upgrade/CMakeLists.txt']:
            self.write(sdk / name, '# Image implementation\n')
        for image in ['preloader', 'loader', 'upgrade']:
            (sdk / f'app/product/demo_foc/boot/{image}').mkdir(parents=True)
        self.write(sdk / 'tools/cmake/modules/image_config.cmake',
                   (ROOT / 'tools/cmake/modules/image_config.cmake').read_text(encoding='utf-8'))
        hardware = sdk / 'app/product/demo_foc/config'
        self.write(hardware / 'board.dts', '/ {};\n')
        self.write(hardware / 'board_defconfig', '# Hardware defaults\n')
        for image in ['app', 'preloader', 'loader', 'upgrade']:
            selected = self.configure(image, sdk=sdk)
            self.assertEqual(selected[3], hardware.as_posix())
            self.assertEqual(selected[4], (hardware / 'board.dts').as_posix())
            self.assertEqual(selected[1].split(';')[0], (hardware / 'board_defconfig').as_posix())

    def test_adding_config_hardware_regenerates_configuration(self):
        self.configure()
        self.write(self.config / 'board.dts', '/ {};\n')
        self.write(self.config / 'board_defconfig', '# Hardware defaults\n')
        result = subprocess.run(['cmake', '--build', str(self.build)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        selected = (self.build / 'selected.txt').read_text(encoding='utf-8').splitlines()
        self.assertEqual(selected[3], self.config.as_posix())
        self.assertEqual(selected[1].split(';')[0], (self.config / 'board_defconfig').as_posix())

    def test_independent_application_can_own_hardware(self):
        application = self.root / 'independent app'
        self.write(application / 'CMakeLists.txt', '# Application implementation\n')
        self.write(application / 'config/board.dts', '/ {};\n')
        selected = self.configure(definitions=[f'-DAPPLICATION={application.as_posix()}'])
        self.assertEqual(selected[3], (application / 'config').as_posix())

    def test_separate_board_selection_is_rejected(self):
        output = self.configure(definitions=['-DBOARD=gd32_demo'], accept=False)
        self.assertIn('Select the product with -Dp', output)

    def test_explicit_fragments_and_overlays_keep_order(self):
        for name in ['alternative.conf', 'extra.conf', 'app.overlay', 'first.overlay', 'second.overlay', 'unused.overlay']:
            self.write(self.config / name, '# Configuration\n' if name.endswith('.conf') else '/ {};\n')
        selected = self.configure(definitions=['-DCONF_FILE=alternative.conf', '-DEXTRA_CONF_FILE=extra.conf',
                                               '-DDTC_OVERLAY_FILE=first.overlay;second.overlay'])
        self.assertEqual(selected[1].split(';')[-2:], [
            (self.config / 'alternative.conf').as_posix(), (self.config / 'extra.conf').as_posix()])
        self.assertEqual(selected[2].split(';'), [
            (self.config / name).as_posix() for name in ['app.overlay', 'first.overlay', 'second.overlay']])

    def test_missing_requested_inputs_fail_closed(self):
        for argument, expected in [('-DPROFILE=missing', 'Missing missing profile'),
                                    ('-DDTC_OVERLAY_FILE=missing.overlay', 'Image configuration file not found')]:
            with self.subTest(argument=argument):
                output = self.configure(definitions=['-UPROFILE', '-UDTC_OVERLAY_FILE', argument], accept=False)
                self.assertIn(expected, output)

    def test_adding_default_overlay_regenerates_configuration(self):
        self.configure()
        overlay = self.config / 'app.overlay'
        self.write(overlay, '/ {};\n')
        result = subprocess.run(['cmake', '--build', str(self.build)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        selected = (self.build / 'selected.txt').read_text(encoding='utf-8')
        self.assertIn(overlay.as_posix(), selected)


if __name__ == '__main__':
    unittest.main()
