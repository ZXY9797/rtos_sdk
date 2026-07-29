#!/usr/bin/env python3

import os
import sys
import unittest
from types import SimpleNamespace


SCRIPT_DIR = os.path.dirname(
    os.path.dirname(os.path.abspath(__file__)))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from device_bindings import parse_adapter
from gen_device_traits import (
    render_header,
    render_source,
    resolve_dependencies,
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

    def test_legacy_isr_flag_is_rejected(self):
        driver = valid_driver()
        driver['isr'] = True
        with self.assertRaisesRegex(ValueError, 'unknown cxx-driver'):
            parse_adapter(driver, 'test.yaml', 'vendor,test')


class GeneratedCodeTest(unittest.TestCase):
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
        self.assertIn('SYS_INIT(hal::_init_test0', source)


if __name__ == '__main__':
    unittest.main()
