#!/usr/bin/env python3
"""Generate DeviceTrait code directly from an edtlib.EDT model."""

import argparse
import json
import sys

from device_bindings import (
    DEFAULT_INIT_LEVEL,
    DEFAULT_INIT_PRIORITY,
    INIT_LEVELS,
    driver_for_node,
    normalize_init_level,
    scan_bindings,
)
from device_codegen_common import (
    load_edt,
    node_alias,
    node_enabled,
    node_identifier,
    str_to_identifier,
    write_if_changed,
)


INIT_LEVEL_ORDER = {
    level: index for index, level in enumerate(INIT_LEVELS)
}


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument('--edt-pickle', required=True)
    parser.add_argument('--bindings-dir', required=True)
    parser.add_argument('--header-out', required=True)
    parser.add_argument('--source-out', required=True)
    parser.add_argument('--report-out', required=True)
    return parser.parse_args()


def property_value(node, property_name, default=None):
    prop = node.props.get(property_name)
    return prop.val if prop is not None else default


def dependency_record(node, reason):
    if node is None or node.dep_ordinal is None:
        raise ValueError(
            f'{reason} does not reference an EDT node with an ordinal')
    return {
        'node': node,
        'node_id': node_identifier(node),
        'ord': node.dep_ordinal,
        'alias': node_alias(node),
        'reason': reason,
    }


def resolve_dependencies(node, requires):
    dependencies = []
    seen = set()

    def add(target, reason):
        record = dependency_record(target, reason)
        if record['ord'] in seen:
            return
        seen.add(record['ord'])
        dependencies.append(record)

    for dependency_type, property_name in requires:
        if dependency_type == 'parent':
            if node.parent is None:
                raise ValueError(
                    f'{node.path}: required parent does not exist')
            add(node.parent, 'parent')
            continue

        prop = node.props.get(property_name)
        if prop is None:
            raise ValueError(
                f'{node.path}: required property '
                f'{property_name!r} is missing')

        if dependency_type == 'phandle':
            target = prop.val
            if not hasattr(target, 'dep_ordinal'):
                raise ValueError(
                    f'{node.path}: {property_name!r} is not a phandle')
            add(target, f'phandle:{property_name}')
            continue

        if dependency_type == 'phandle_array':
            if not isinstance(prop.val, list):
                raise ValueError(
                    f'{node.path}: {property_name!r} '
                    f'is not a phandle-array')
            for entry in prop.val:
                target = getattr(entry, 'controller', None)
                if target is None:
                    raise ValueError(
                        f'{node.path}: malformed phandle-array '
                        f'{property_name!r}')
                add(target, f'phandle_array:{property_name}')
            continue

        raise ValueError(
            f'{node.path}: unsupported dependency type '
            f'{dependency_type!r}')

    return dependencies


def first_irq(node):
    if not node.interrupts:
        return None
    irq = node.interrupts[0].data.get('irq')
    return int(irq) if irq is not None else None


def generate_specs(edt, drivers):
    specs = []
    used_headers = set()

    for node in sorted(
            edt.nodes,
            key=lambda item: (
                item.dep_ordinal is None,
                item.dep_ordinal or 0)):
        driver = driver_for_node(node, drivers)
        if driver is None:
            continue

        enabled = node_enabled(node)
        if node.dep_ordinal is None:
            raise ValueError(
                f'{node.path}: C++ device has no dependency ordinal')
        if enabled and driver['has_isr'] and first_irq(node) is None:
            raise ValueError(
                f'{node.path}: ISR adapter requires an interrupt')

        spec = {
            'node': node,
            'ord': node.dep_ordinal,
            'alias': node_alias(node),
            'type': driver['type_name'],
            'header': driver['adapter_header'],
            'adapter_macro': driver['adapter_macro'],
            'enabled': enabled,
            'node_id': node_identifier(node),
            'path': node.path,
            'compatible': driver['compatible'],
            'binding_path': driver['binding_path'],
            'readiness': driver['readiness'],
            'device_base': driver['device_base'],
            'dependencies': resolve_dependencies(
                node, driver['requires']),
            'init_level': driver['init_level'],
            'init_priority': driver['init_priority'],
            'has_init': driver['has_init'],
            'has_isr': driver['has_isr'],
            'irq': first_irq(node),
        }
        specs.append(spec)
        used_headers.add(driver['adapter_header'])

    return specs, used_headers


def resolve_init_priority(spec):
    priority = property_value(
        spec['node'], 'init-priority', spec['init_priority'])
    try:
        priority = int(priority)
    except (TypeError, ValueError) as error:
        raise ValueError(
            f'{spec["path"]}: invalid init-priority '
            f'{priority!r}') from error
    if not 0 <= priority <= 999:
        raise ValueError(
            f'{spec["path"]}: init-priority out of range: '
            f'{priority}')
    return priority


def resolve_init_level_key(spec):
    level = property_value(
        spec['node'], 'init-level', spec['init_level'])
    try:
        return normalize_init_level(level)
    except ValueError as error:
        raise ValueError(
            f'{spec["path"]}: invalid init-level: {error}') from error


def resolve_init_level(spec):
    return INIT_LEVELS[resolve_init_level_key(spec)]


def check_init_dependencies(specs):
    spec_by_ordinal = {spec['ord']: spec for spec in specs}
    errors = []

    for spec in specs:
        if not spec['enabled']:
            continue

        source_level = resolve_init_level_key(spec)
        source_priority = resolve_init_priority(spec)
        source_order = (
            INIT_LEVEL_ORDER[source_level], source_priority)
        source_name = spec['alias'] or spec['path']

        for dependency in spec['dependencies']:
            target = spec_by_ordinal.get(dependency['ord'])
            if target is None or not target['enabled']:
                continue

            target_level = resolve_init_level_key(target)
            target_priority = resolve_init_priority(target)
            target_order = (
                INIT_LEVEL_ORDER[target_level], target_priority)
            target_name = target['alias'] or target['path']

            if source_order <= target_order:
                errors.append(
                    f'{source_name} (level={source_level}, '
                    f'prio={source_priority}) depends on '
                    f'{target_name} (level={target_level}, '
                    f'prio={target_priority}) via '
                    f'{dependency["reason"]}, but does not '
                    f'initialize later')

    if errors:
        raise ValueError(
            'invalid init dependencies:\n  ' +
            '\n  '.join(errors))


def append_registry(lines, specs):
    enabled_specs = [
        spec for spec in specs if spec['enabled']]
    if not enabled_specs:
        return

    for spec in enabled_specs:
        ordinal = spec['ord']
        identifier = str_to_identifier(
            spec['alias'] or f'ord{ordinal}')
        lines.extend([
            f'inline bool _check_{identifier}(void *instance) {{',
            f'    auto *device = static_cast<DeviceTrait<{ordinal}>::'
            f'type *>(instance);',
            '    return detail::device_ready(*device);',
            '}',
        ])

    lines.extend([
        '// Device registry for diagnostics and runtime enumeration.',
        'inline const DeviceInfo s_device_registry[] = {',
    ])
    for spec in enabled_specs:
        ordinal = spec['ord']
        alias = spec['alias'] or f'ord{ordinal}'
        type_name = spec['type'].split('::')[-1]
        check = f'_check_{str_to_identifier(alias)}'
        lines.append(
            f'    {{ .ord = {ordinal}, .alias = "{alias}", '
            f'.type_name = "{type_name}", '
            f'.instance = &DeviceTrait<{ordinal}>::instance, '
            f'.is_ready = {check} }},')

    lines.extend([
        '};',
        '',
        'inline const DeviceInfo *get_device_registry(size_t *count) {',
        f'    *count = {len(enabled_specs)};',
        '    return s_device_registry;',
        '}',
        '',
    ])


def render_header(specs, used_headers):
    lines = [
        '// Auto-generated by gen_device_traits.py. DO NOT EDIT.',
        '#pragma once',
        '',
        '#include <device.h>',
    ]
    lines.extend(
        f'#include <{header}>'
        for header in sorted(used_headers))
    lines.extend([
        '',
        'namespace hal {',
        '',
    ])

    for spec in specs:
        if not spec['enabled']:
            continue
        comment = spec['alias'] or spec['path']
        lines.extend([
            f'// {comment}',
            f'{spec["adapter_macro"]}({spec["node_id"]})',
            '',
        ])

    lines.extend([
        'template <int Ord>',
        'inline auto &device_get() {',
        '    return DeviceTrait<Ord>::instance;',
        '}',
        '',
    ])
    append_registry(lines, specs)
    lines.extend([
        '} // namespace hal',
        '',
        '#define device_get(alias) \\',
        '    hal::device_get<DT_ORD(DT_ALIAS(alias))>()',
        '',
        '// Instance markers used by MCU-specific driver sources.',
    ])

    for spec in specs:
        if not spec['enabled']:
            continue
        suffix = spec['node_id'].split('_')[-1].upper()
        safe_type = spec['type'].replace('::', '_')
        lines.append(f'#define DT_INST_{safe_type}_{suffix}')
    lines.append('')
    return '\n'.join(lines)


def render_source(specs):
    lines = [
        '// Auto-generated by gen_device_traits.py. DO NOT EDIT.',
        '',
        '#include <drivers_generated.h>',
        '#include <init.h>',
        '',
        'namespace hal {',
        '',
    ]
    init_functions = []

    for spec in specs:
        if not spec['enabled']:
            continue

        ordinal = spec['ord']
        alias = spec['alias'] or f'ord{ordinal}'
        safe_name = str_to_identifier(alias)
        lines.extend([
            f'// {alias}',
            f'DeviceTrait<{ordinal}>::type '
            f'DeviceTrait<{ordinal}>::instance{{}};',
            '',
        ])

        if spec['has_init']:
            function_name = f'_init_{safe_name}'
            lines.extend([
                f'static int {function_name}() {{',
                f'    return DeviceTrait<{ordinal}>::init();',
                '}',
                '',
            ])
            init_functions.append((
                function_name,
                resolve_init_level(spec),
                resolve_init_priority(spec),
            ))

    lines.extend([
        '} // namespace hal',
        '',
    ])

    for spec in specs:
        if not spec['enabled'] or not spec['has_isr']:
            continue
        alias = spec['alias'] or f'ord{spec["ord"]}'
        lines.extend([
            f'// {alias} ISR',
            f'extern "C" void IRQ{spec["irq"]}_Handler(void) {{',
            f'    hal::DeviceTrait<{spec["ord"]}>::isr();',
            '}',
            '',
        ])

    for function_name, level, priority in init_functions:
        lines.append(
            f'SYS_INIT(hal::{function_name}, {level}, {priority});')
    lines.append('')
    return '\n'.join(lines)


def render_report(specs):
    spec_by_ordinal = {spec['ord']: spec for spec in specs}
    devices = []

    for spec in specs:
        dependencies = []
        for dependency in spec['dependencies']:
            target = spec_by_ordinal.get(dependency['ord'])
            dependencies.append({
                'ord': dependency['ord'],
                'alias': dependency['alias'],
                'node_id': dependency['node_id'],
                'path': dependency['node'].path,
                'reason': dependency['reason'],
                'has_cxx_driver': target is not None,
                'enabled': node_enabled(
                    dependency['node']),
                'init_level': (
                    resolve_init_level_key(target)
                    if target else None),
                'init_priority': (
                    resolve_init_priority(target)
                    if target else None),
            })

        devices.append({
            'ord': spec['ord'],
            'alias': spec['alias'],
            'node_id': spec['node_id'],
            'path': spec['path'],
            'compatible': spec['compatible'],
            'type': spec['type'],
            'header': spec['header'],
            'enabled': spec['enabled'],
            'init_level': resolve_init_level_key(spec),
            'init_priority': resolve_init_priority(spec),
            'readiness': spec['readiness'],
            'device_base': spec['device_base'],
            'binding': spec['binding_path'],
            'dependencies': dependencies,
        })

    report = {
        'schema': 'rtos-sdk.devices.v2',
        'device_count': len(devices),
        'enabled_count': sum(
            1 for device in devices if device['enabled']),
        'devices': devices,
    }
    return json.dumps(
        report, ensure_ascii=False, indent=2) + '\n'


def main():
    args = parse_args()
    try:
        edt = load_edt(args.edt_pickle)
        drivers = scan_bindings(args.bindings_dir)
        specs, used_headers = generate_specs(edt, drivers)
        check_init_dependencies(specs)

        write_if_changed(
            args.header_out,
            render_header(specs, used_headers))
        write_if_changed(
            args.source_out,
            render_source(specs))
        write_if_changed(
            args.report_out,
            render_report(specs))
    except (OSError, ValueError) as error:
        print(f'gen_device_traits.py: error: {error}',
              file=sys.stderr)
        return 1

    print(
        f'Generated {len(specs)} DeviceTrait entries from EDT')
    return 0


if __name__ == '__main__':
    sys.exit(main())
