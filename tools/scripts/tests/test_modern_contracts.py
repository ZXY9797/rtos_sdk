from pathlib import Path
import json
import sys
import tempfile
import unittest
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from device_bindings import parse_adapter
from gen_device_traits import collect_resources, stable_device_id
from sdk_artifacts import device_graph, write_changed, normalize_paths
from dependency_inventory import tree_digest
from decode_system_diagnostics import decode
import struct


class ModernContractsTest(unittest.TestCase):
    def test_build_directory_is_normalized_before_sdk_root(self):
        root = Path('/workspace/sdk')
        build = root / 'out/one'
        self.assertEqual(normalize_paths('/workspace/sdk/out/one/include /workspace/sdk/src', root, build),
                         'BUILD_ROOT/include SDK_ROOT/src')

    def test_diagnostic_truncation_and_unknown_record_handling(self):
        header = (b'\x01\x01x' + struct.pack('<IBIIBIIHB', 10, 1, 200, 100, 0, 0, 0, 1, 0))
        result = decode(header)
        self.assertTrue(result['truncated'])
        device = b'\x01' + struct.pack('<H', 9) + struct.pack('<IIB', 42, 3, 1)
        result = decode(header + device + b'\x63\x01\x00x')
        self.assertFalse(result['truncated'])
        self.assertEqual(result['devices'][0]['id'], 42)
        self.assertEqual(result['unknown'][0]['type'], 99)
        with self.assertRaisesRegex(ValueError, 'truncated'):
            decode(header + device[:-1])
    def test_exclusive_dma_resource_conflict_names_both_owners(self):
        controller = SimpleNamespace(path='/dma0')
        def node(path, channel, status='okay'):
            entry = SimpleNamespace(controller=controller, data={'channel': channel})
            return SimpleNamespace(path=path, status=status,
                props={'dmas': SimpleNamespace(name='dmas', val=[entry])}, parent=None)
        with self.assertRaisesRegex(ValueError, '/spi0 and /uart0'):
            collect_resources(SimpleNamespace(nodes=[node('/spi0', 2), node('/uart0', 2)]))
        with self.assertRaisesRegex(ValueError, '/spi0 and /spi0'):
            collect_resources(SimpleNamespace(nodes=[node('/spi0', 2), node('/spi0', 2)]))
        self.assertEqual(len(collect_resources(SimpleNamespace(nodes=[
            node('/spi0', 2), node('/uart0', 3)]))), 2)
        self.assertEqual(len(collect_resources(SimpleNamespace(nodes=[
            node('/spi0', 2), node('/uart0', 2, 'disabled')]))), 1)

    def test_capability_schema_rejects_typo(self):
        data = {'adapter': {'header': 'test.h', 'macro': 'TEST'}, 'type-name': 'Test',
                'capabilities': ['async-transfer', 'dma']}
        self.assertEqual(parse_adapter(data, 'test.yaml', 'test,driver')['capabilities'],
                         ['async-transfer', 'dma'])
        data['capabilities'] = ['async-transfre']
        with self.assertRaises(ValueError): parse_adapter(data, 'test.yaml', 'test,driver')

    def test_device_ids_depend_on_path_not_ordinal(self):
        self.assertNotEqual(stable_device_id('/spi@1000'), stable_device_id('/spi@2000'))
        self.assertEqual(stable_device_id('/spi@1000'), stable_device_id('/spi@1000'))

    def test_graph_escapes_alias_and_keeps_dependency_direction(self):
        report = {'devices': [{'enabled': True, 'ord': 2, 'alias': 'sensor"0', 'path': '/sensor',
                               'dependencies': [{'ord': 1, 'reason': 'parent'}]}]}
        graph = device_graph(report)
        self.assertIn('n1 -> n2', graph)
        self.assertIn('sensor\\"0', graph)

    def test_inventory_normalizes_line_endings_and_detects_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            path = root / 'source.c'
            path.write_bytes(b'a\r\nb\r\n')
            initial = tree_digest(root)
            path.write_bytes(b'a\nb\n')
            self.assertEqual(initial, tree_digest(root))
            path.write_bytes(b'changed\n')
            self.assertNotEqual(initial, tree_digest(root))

    def test_artifact_write_is_atomic_and_preserves_unchanged_file(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'info.json'
            write_changed(path, json.dumps({'valid': True}))
            timestamp = path.stat().st_mtime_ns
            write_changed(path, json.dumps({'valid': True}))
            self.assertEqual(timestamp, path.stat().st_mtime_ns)
            self.assertFalse(path.with_suffix('.json.tmp').exists())


if __name__ == '__main__': unittest.main()
