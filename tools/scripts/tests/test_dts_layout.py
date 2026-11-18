from pathlib import Path
import sys
from types import SimpleNamespace as Node
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_dts_layout import validate_layout, load_layout


class DtsLayoutTest(unittest.TestCase):
    def setUp(self):
        self.layout = load_layout('demo_foc')
        path = f'/flash@{self.layout["flash_base"]:x}'
        self.flash = Node(path=path, status='okay', regs=[Node(addr=self.layout['flash_base'], size=self.layout['flash_size'])],
                          props={name: Node(val=self.layout[name.replace('-', '_')])
                                 for name in ('erase-block-size', 'write-block-size')})
        self.partitions = Node(path=path + '/partitions', status='okay', children={})
        for name, part in self.layout['partitions'].items():
            self.partitions.children[name] = Node(
                path=self.partitions.path + f'/partition@{part["offset"]:x}', status='okay',
                props={'label': Node(val=name)}, regs=[Node(addr=part['offset'], size=part['size'])])
        self.chosen = Node(path='/chosen', props={'zephyr,flash-partition': Node(val=self.partitions.children['slot0'])})
        self.edt = Node(nodes=[self.flash, self.partitions, self.chosen])

    def test_shared_layout_matches(self):
        validate_layout(self.edt, self.layout, 'app')

    def test_extra_or_disabled_partition_is_rejected(self):
        self.partitions.children['unexpected'] = Node(path='/extra', status='okay',
            props={'label': Node(val='unexpected')}, regs=[Node(addr=0, size=2048)])
        with self.assertRaisesRegex(ValueError, 'DTS partitions differ'):
            validate_layout(self.edt, self.layout, 'app')
        del self.partitions.children['unexpected']
        self.partitions.children['loader'].status = 'disabled'
        with self.assertRaisesRegex(ValueError, 'invalid or duplicate'):
            validate_layout(self.edt, self.layout, 'loader')

    def test_geometry_change_is_rejected(self):
        self.flash.regs[0].size += 2048
        with self.assertRaisesRegex(ValueError, 'Flash address/size differs'):
            validate_layout(self.edt, self.layout, 'app')

    def test_app_cannot_select_another_partition(self):
        self.chosen.props['zephyr,flash-partition'].val = self.partitions.children['loader']
        with self.assertRaisesRegex(ValueError, 'linked slot0'):
            validate_layout(self.edt, self.layout, 'app')


if __name__ == '__main__':
    unittest.main()
