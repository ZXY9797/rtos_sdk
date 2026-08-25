#!/usr/bin/env python3

import os
import re
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace


SCRIPT_DIR = os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from device_bindings import parse_adapter
from gen_device_traits import (
    check_init_dependencies,
    render_header,
    render_report,
    render_source,
    resolve_dependencies,
    resolve_interrupts,
)


def valid_driver():
    return {
        'adapter': {
            'header': 'device_adapters/test_dt.h',
            'macro': 'HAL_TEST_DT_ADAPT',
        },
        'type-name': 'TestDriver',
    }


def fake_node(path='/test'):
    return SimpleNamespace(
        path=path,
        props={},
        parent=None,
        aliases=[],
        status='okay',
        dep_ordinal=1,
    )


def fake_spec(has_init=False, interrupts=None):
    node = fake_node()
    return {
        'node': node,
        'ord': 1,
        'alias': 'test0',
        'type': 'TestDriver',
        'adapter_macro': 'HAL_TEST_DT_ADAPT',
        'enabled': True,
        'node_id': 'DT_N_S_test',
        'path': '/test',
        'init_level': 'pre-kernel-2',
        'init_priority': 25,
        'has_init': has_init,
        'has_isr': bool(interrupts),
        'interrupts': interrupts or [],
        'readiness': 'auto',
    }


class BindingContractTest(unittest.TestCase):
    def test_unknown_key_is_rejected(self):
        driver = valid_driver()
        driver['args'] = ['node-reg']
        with self.assertRaisesRegex(ValueError, 'unknown cxx-driver'):
            parse_adapter(driver, 'test.yaml', 'vendor,test')

    def test_legacy_dsl_is_rejected(self):
        driver = {
            'type-name': 'TestDriver',
        }
        with self.assertRaisesRegex(ValueError, 'legacy'):
            parse_adapter(driver, 'test.yaml', 'vendor,test')

    def test_missing_required_dependency_is_rejected(self):
        node = fake_node()
        with self.assertRaisesRegex(ValueError, 'required property'):
            resolve_dependencies(
                node, [('phandle', 'controller')])

    def test_external_dependency_lifecycle_is_parsed(self):
        driver = valid_driver()
        driver['requires'] = [{
            'phandle-array': 'dmas',
            'lifecycle': 'external',
        }]
        parsed = parse_adapter(
            driver, 'test.yaml', 'vendor,test')
        self.assertEqual(
            parsed['requires'],
            [('phandle_array', 'dmas', 'external')])

    def test_invalid_dependency_lifecycle_is_rejected(self):
        driver = valid_driver()
        driver['requires'] = [{
            'phandle': 'clock',
            'lifecycle': 'implicit',
        }]
        with self.assertRaisesRegex(ValueError, 'invalid dependency lifecycle'):
            parse_adapter(driver, 'test.yaml', 'vendor,test')

    def test_interrupt_metadata_is_structured(self):
        driver = valid_driver()
        driver['interrupts'] = [{
            'name': 'global',
            'method': 'isr_global',
            'uses-osal': True,
        }]
        parsed = parse_adapter(
            driver, 'test.yaml', 'vendor,test')
        self.assertEqual(
            parsed['interrupts'][0]['method'], 'isr_global')
        self.assertTrue(
            parsed['interrupts'][0]['uses_osal'])
        self.assertFalse(
            parsed['interrupts'][0]['enable_on_init'])

    def test_phandle_array_interrupt_source_is_parsed(self):
        driver = valid_driver()
        driver['interrupts'] = [{
            'name': 'dma-tx',
            'method': 'isr_dma_tx',
            'enable-on-init': True,
            'source': {
                'phandle-array': 'dmas',
                'entry': 'tx',
                'interrupt-index-cell': 'channel',
            },
        }]
        parsed = parse_adapter(
            driver, 'test.yaml', 'vendor,test')
        interrupt = parsed['interrupts'][0]
        self.assertEqual(
            interrupt['source']['phandle-array'], 'dmas')
        self.assertTrue(interrupt['enable_on_init'])

    def test_legacy_isr_flag_is_rejected(self):
        driver = valid_driver()
        driver['isr'] = True
        with self.assertRaisesRegex(ValueError, 'unknown cxx-driver'):
            parse_adapter(driver, 'test.yaml', 'vendor,test')


class GeneratedCodeTest(unittest.TestCase):
    def test_missing_generated_lifecycle_owner_is_rejected(self):
        dependency_node = fake_node('/dma0')
        dependency_node.dep_ordinal = 2
        spec = fake_spec(has_init=True)
        spec['dependencies'] = [{
            'node': dependency_node,
            'ord': 2,
            'reason': 'phandle_array:dmas',
            'lifecycle': 'generated',
        }]
        with self.assertRaisesRegex(
                ValueError, r'no generated C\+\+ lifecycle owner'):
            check_init_dependencies([spec])

    def test_explicit_external_lifecycle_owner_is_accepted(self):
        dependency_node = fake_node('/dma0')
        dependency_node.dep_ordinal = 2
        spec = fake_spec(has_init=True)
        spec['dependencies'] = [{
            'node': dependency_node,
            'ord': 2,
            'reason': 'phandle_array:dmas',
            'lifecycle': 'external',
        }]
        check_init_dependencies([spec])

    def test_external_lifecycle_cannot_hide_generated_owner(self):
        dependency_node = fake_node('/dma0')
        dependency_node.dep_ordinal = 2
        source = fake_spec(has_init=True)
        source['dependencies'] = [{
            'node': dependency_node,
            'ord': 2,
            'reason': 'phandle_array:dmas',
            'lifecycle': 'external',
        }]
        target = fake_spec(has_init=True)
        target.update({
            'node': dependency_node,
            'ord': 2,
            'alias': 'dma0',
            'path': '/dma0',
            'dependencies': [],
        })
        with self.assertRaisesRegex(
                ValueError, r'generated C\+\+ lifecycle owner'):
            check_init_dependencies([source, target])

    def test_registry_honors_all_readiness_contracts(self):
        specs = []
        for ordinal, readiness in enumerate((
                'auto', 'device-base', 'is-initialized',
                'always-ready'), start=1):
            spec = fake_spec()
            spec.update({
                'ord': ordinal,
                'alias': f'test{ordinal}',
                'readiness': readiness,
            })
            specs.append(spec)

        header = render_header(
            specs, {'device_adapters/test_dt.h'})
        self.assertIn('return detail::device_ready(*device);', header)
        self.assertIn(
            'readiness=device-base requires DeviceBase', header)
        self.assertIn(
            'readiness=is-initialized requires a const ', header)
        self.assertIn('(void)instance;', header)
        self.assertIn('bool _check_test1(const void *instance)', header)

    def test_empty_registry_is_safe(self):
        header = render_header([], set())
        self.assertIn('if (count != nullptr)', header)
        self.assertIn('return nullptr;', header)

    def test_report_schema_tracks_interrupt_source_metadata(self):
        spec = fake_spec()
        spec.update({
            'dependencies': [],
            'compatible': 'vendor,test',
            'header': 'device_adapters/test_dt.h',
            'binding_path': 'test.yaml',
            'readiness': 'always-ready',
            'device_base': False,
        })
        report = render_report([spec])
        self.assertIn('"schema": "rtos-sdk.devices.v5"', report)

    def test_drivers_do_not_declare_vector_handlers(self):
        repo_root = Path(SCRIPT_DIR).parents[1]
        drivers_dir = repo_root / 'embedded' / 'drivers'
        vector_handler = re.compile(
            r'(?:extern\s+"C"\s+)?void\s+'
            r'(?:IRQ\d+_Handler|[A-Za-z0-9_]+_IRQHandler)\s*\(')
        offenders = []
        for path in drivers_dir.rglob('*'):
            if path.suffix not in {'.c', '.cc', '.cpp'}:
                continue
            if vector_handler.search(
                    path.read_text(encoding='utf-8')):
                offenders.append(str(path.relative_to(repo_root)))
        self.assertEqual(
            offenders, [],
            'vector handlers must be generated from EDT: '
            + ', '.join(offenders))

    def test_drivers_do_not_own_irq_configuration(self):
        repo_root = Path(SCRIPT_DIR).parents[1]
        roots = [
            repo_root / 'embedded' / 'drivers',
            repo_root / 'embedded' / 'include' / 'drivers',
            repo_root / 'embedded' / 'include' / 'device_adapters',
        ]
        irq_ownership = re.compile(
            r'\b(?:'
            r'Irq::(?:connect|enable|disable|clearPending|setPriority)'
            r'|DT_IRQN|[A-Za-z0-9_]+_IRQn'
            r')\b')
        offenders = []
        for root in roots:
            for path in root.rglob('*'):
                if path.suffix not in {
                        '.c', '.cc', '.cpp', '.h', '.hpp'}:
                    continue
                if irq_ownership.search(
                        path.read_text(encoding='utf-8')):
                    offenders.append(str(path.relative_to(repo_root)))
        self.assertEqual(
            offenders, [],
            'IRQ configuration belongs to EDT code generation: '
            + ', '.join(offenders))

    def test_no_init_device_still_has_instance_definition(self):
        source = render_source([fake_spec(has_init=False)])
        self.assertIn(
            'DeviceTrait<1>::type DeviceTrait<1>::instance{};',
            source)
        self.assertNotIn('_init_test0', source)

    def test_header_has_no_explicit_template_instantiation(self):
        header = render_header(
            [fake_spec(has_init=False)],
            {'device_adapters/test_dt.h'})
        self.assertNotIn('template class', header)

    def test_interrupt_only_device_gets_irq_init_and_wrapper(self):
        interrupt = {
            'name': 'global',
            'method': 'isr_global',
            'uses_osal': True,
            'shared': False,
            'enable_on_init': True,
            'source': None,
            'irq': 37,
            'priority': 6,
            'controller': '/nvic',
            'index': 0,
        }
        source = render_source([
            fake_spec(has_init=False, interrupts=[interrupt])])
        self.assertIn('Irq::connect(37, IRQ37_Handler);', source)
        self.assertIn(
            'hal::DeviceTrait<1>::isr_global(context);', source)
        self.assertIn('Irq::enable(37);', source)
        self.assertIn('SYS_INIT_ROLLBACK(hal::_init_test0', source)
        self.assertIn('hal::_deinit_test0', source)

    def test_dma_interrupt_is_resolved_from_controller_channel(self):
        controller_irq = SimpleNamespace(
            data={'irq': 14, 'priority': 6},
            controller=SimpleNamespace(path='/nvic'),
            name=None)
        dma_controller = SimpleNamespace(
            path='/dma0', interrupts=[
                controller_irq, controller_irq, controller_irq,
                controller_irq])
        dma_entry = SimpleNamespace(
            name='tx',
            controller=dma_controller,
            data={'channel': 3, 'request-id': 11})
        node = fake_node('/spi0')
        node.interrupts = []
        node.props = {
            'dmas': SimpleNamespace(val=[dma_entry]),
        }
        declaration = {
            'name': 'dma-tx',
            'method': 'isr_dma_tx',
            'uses_osal': True,
            'shared': False,
            'enable_on_init': True,
            'source': {
                'phandle-array': 'dmas',
                'entry': 'tx',
                'interrupt-index-cell': 'channel',
            },
        }
        interrupt = resolve_interrupts(
            node, [declaration])[0]
        self.assertEqual(interrupt['irq'], 14)
        self.assertEqual(interrupt['index'], 3)

    def test_single_direct_irq_fallback_ignores_indirect_declarations(self):
        direct_irq = SimpleNamespace(
            data={'irq': 37, 'priority': 6},
            controller=SimpleNamespace(path='/nvic'),
            name=None)
        dma_irq = SimpleNamespace(
            data={'irq': 60, 'priority': 6},
            controller=SimpleNamespace(path='/nvic'),
            name=None)
        dma_controller = SimpleNamespace(
            path='/dma1',
            interrupts=[dma_irq, dma_irq, dma_irq, dma_irq, dma_irq])
        node = fake_node('/uart0')
        node.interrupts = [direct_irq]
        node.props = {
            'dmas': SimpleNamespace(val=[
                SimpleNamespace(
                    name='tx',
                    controller=dma_controller,
                    data={'channel': 4, 'request-id': 21}),
            ]),
        }
        declarations = [
            {
                'name': 'global',
                'method': 'isr_global',
                'uses_osal': True,
                'shared': False,
                'enable_on_init': True,
                'source': None,
            },
            {
                'name': 'dma-tx',
                'method': 'isr_dma_tx',
                'uses_osal': True,
                'shared': False,
                'enable_on_init': True,
                'source': {
                    'phandle-array': 'dmas',
                    'entry': 'tx',
                    'interrupt-index-cell': 'channel',
                },
            },
        ]
        interrupts = resolve_interrupts(node, declarations)
        self.assertEqual(
            [interrupt['irq'] for interrupt in interrupts],
            [37, 60])


if __name__ == '__main__':
    unittest.main()
