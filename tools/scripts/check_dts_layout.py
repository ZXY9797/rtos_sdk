#!/usr/bin/env python3
"""Check the merged device tree against the shared image layout manifest."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

from device_codegen_common import load_edt

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'bootloader/scripts'))
from boot_layout import load_layout


def validate_layout(edt, layout, firmware_type):
    flash_path = f'/flash@{layout["flash_base"]:x}'
    nodes = {node.path: node for node in edt.nodes}
    flash = nodes.get(flash_path)
    if flash is None or flash.status != 'okay' or len(flash.regs) != 1:
        raise ValueError('canonical Flash node is missing or disabled')
    if (flash.regs[0].addr, flash.regs[0].size) != (layout['flash_base'], layout['flash_size']):
        raise ValueError('Flash address/size differs from layout.json')
    for field in ('erase-block-size', 'write-block-size'):
        prop = flash.props.get(field)
        if prop is None or prop.val != layout[field.replace('-', '_')]:
            raise ValueError(f'{field} differs from layout.json')

    partitions = nodes.get(flash_path + '/partitions')
    if partitions is None or partitions.status != 'okay':
        raise ValueError('canonical partitions are missing or disabled')
    found = {}
    for child in partitions.children.values():
        label = child.props.get('label')
        if label is None or label.val in found or len(child.regs) != 1 or child.status != 'okay':
            raise ValueError(f'invalid or duplicate partition: {child.path}')
        found[label.val] = {'offset': child.regs[0].addr, 'size': child.regs[0].size}
    if found != layout['partitions']:
        raise ValueError('DTS partitions differ from layout.json; change the shared manifest')

    chosen = nodes.get('/chosen')
    selected = chosen.props.get('zephyr,flash-partition') if chosen is not None else None
    if firmware_type == 'app' and selected is not None:
        slot0 = layout['partitions']['slot0']['offset']
        if selected.val.path != f'{flash_path}/partitions/partition@{slot0:x}':
            raise ValueError('application chosen partition must match the linked slot0')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--edt-pickle', required=True)
    parser.add_argument('--layout', required=True)
    parser.add_argument('--firmware', required=True, choices=['app', 'preloader', 'loader', 'upgrade'])
    args = parser.parse_args()
    try:
        validate_layout(load_edt(args.edt_pickle), load_layout(args.layout), args.firmware)
    except (OSError, ValueError) as error:
        parser.exit(1, f'layout validation: {error}\n')
    print('Merged DTS matches the shared Flash layout')


if __name__ == '__main__':
    main()
