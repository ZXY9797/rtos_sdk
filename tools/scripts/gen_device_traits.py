#!/usr/bin/env python3
"""Generate DeviceTrait code directly from an edtlib.EDT model."""

import argparse
import json
from pathlib import Path
import sys

from device_bindings import (
    CAPABILITY_BITS,
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

def stable_device_id(path):
    value = 2166136261
    for byte in path.encode('utf-8'):
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def capability_mask(spec):
    return sum(CAPABILITY_BITS[name] for name in spec.get('capabilities', []))


def collect_resources(edt):
    claims = []
    owners = {}
    for node in edt.nodes:
        if not node_enabled(node):
            continue
        for prop in node.props.values():
            if not isinstance(prop.val, list):
                continue
            for entry in prop.val:
                controller = getattr(entry, 'controller', None)
                data = getattr(entry, 'data', {})
                if controller is None:
                    continue
                kind = 'dma' if prop.name == 'dmas' else 'gpio' if 'gpio' in prop.name else None
                cell = 'channel' if kind == 'dma' else 'pin'
                if kind is None or cell not in data:
                    continue
                key = (kind, controller.path, int(data[cell]))
                previous = owners.get(key)
                if previous is not None and (kind == 'dma' or previous != node.path):
                    raise ValueError(f'{kind} resource conflict: {key} used by {previous} and {node.path}')
                owners[key] = node.path
                claims.append({'kind': kind, 'controller': controller.path,
                               'index': int(data[cell]), 'owner': node.path})
    return claims


def parse_args():
    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument('--edt-pickle', required=True)
    parser.add_argument('--config', type=Path, required=True)
    parser.add_argument('--require', action='append', default=[], metavar='ALIAS:CAPABILITY')
    parser.add_argument('--no-instances', action='store_true')
    parser.add_argument('--bindings-dir', required=True, action='append')
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

    def add(target, reason, lifecycle):
        record = dependency_record(target, reason)
        if record['ord'] in seen:
            existing = next(
                item for item in dependencies
                if item['ord'] == record['ord'])
            if existing['lifecycle'] != lifecycle:
                raise ValueError(
                    f'{node.path}: dependency {target.path} has '
                    f'conflicting lifecycle ownership')
            return
        seen.add(record['ord'])
        record['lifecycle'] = lifecycle
        dependencies.append(record)

    for requirement in requires:
        if len(requirement) == 2:
            dependency_type, property_name = requirement
            lifecycle = 'generated'
        else:
            dependency_type, property_name, lifecycle = requirement
        if dependency_type == 'parent':
            if node.parent is None:
                raise ValueError(
                    f'{node.path}: required parent does not exist')
            add(node.parent, 'parent', lifecycle)
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
            add(target, f'phandle:{property_name}', lifecycle)
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
                add(target, f'phandle_array:{property_name}', lifecycle)
            continue

        raise ValueError(
            f'{node.path}: unsupported dependency type '
            f'{dependency_type!r}')

    return dependencies


def direct_interrupt(node, declaration, declaration_count):
    if not node.interrupts:
        raise ValueError(
            f'{node.path}: interrupt metadata requires interrupts')

    named = {
        interrupt.name: (index, interrupt)
        for index, interrupt in enumerate(node.interrupts)
        if interrupt.name is not None
    }
    match = named.get(declaration['name'])
    if match is not None:
        return match
    if declaration_count == 1 and len(node.interrupts) == 1:
        return 0, node.interrupts[0]
    raise ValueError(
        f'{node.path}: interrupt {declaration["name"]!r} '
        f'is not present in EDT')


def phandle_array_interrupt(node, declaration):
    source = declaration['source']
    property_name = source['phandle-array']
    prop = node.props.get(property_name)
    if prop is None or not isinstance(prop.val, list):
        raise ValueError(
            f'{node.path}: interrupt source {property_name!r} '
            f'is missing or is not a phandle-array')

    source_entry = next(
        (entry for entry in prop.val
         if entry.name == source['entry']),
        None)
    if source_entry is None:
        raise ValueError(
            f'{node.path}: {property_name!r} has no entry '
            f'{source["entry"]!r}')

    index_cell = source['interrupt-index-cell']
    interrupt_index = source_entry.data.get(index_cell)
    if not isinstance(interrupt_index, int):
        raise ValueError(
            f'{node.path}: {property_name!r} entry '
            f'{source["entry"]!r} has no integer '
            f'{index_cell!r} cell')

    controller = source_entry.controller
    if not 0 <= interrupt_index < len(controller.interrupts):
        raise ValueError(
            f'{node.path}: interrupt index {interrupt_index} '
            f'from {property_name!r} is out of range for '
            f'{controller.path}')
    return interrupt_index, controller.interrupts[interrupt_index]


def resolve_interrupts(node, declarations):
    resolved = []
    direct_count = sum(
        declaration['source'] is None
        for declaration in declarations)
    for declaration in declarations:
        if declaration['source'] is None:
            index, interrupt = direct_interrupt(
                node, declaration, direct_count)
        else:
            index, interrupt = phandle_array_interrupt(
                node, declaration)

        irq = interrupt.data.get('irq')
        priority = interrupt.data.get('priority')
        if irq is None:
            raise ValueError(
                f'{node.path}: interrupt {declaration["name"]!r} '
                f'has no irq cell')
        if priority is None:
            raise ValueError(
                f'{node.path}: interrupt {declaration["name"]!r} '
                f'has no priority cell')

        irq = int(irq)
        priority = int(priority)
        if irq < 0:
            raise ValueError(
                f'{node.path}: interrupt {declaration["name"]!r} '
                f'has invalid irq {irq}')
        if not 0 <= priority <= 255:
            raise ValueError(
                f'{node.path}: interrupt {declaration["name"]!r} '
                f'has invalid priority {priority}')

        resolved.append({
            **declaration,
            'irq': irq,
            'priority': priority,
            'controller': interrupt.controller.path,
            'index': index,
        })
    return resolved


def generate_specs(edt, drivers, *, instantiate=True):
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

        enabled = instantiate and node_enabled(node)
        if node.dep_ordinal is None:
            raise ValueError(
                f'{node.path}: C++ device has no dependency ordinal')
        interrupts = (
            resolve_interrupts(node, driver['interrupts'])
            if enabled else [])

        capabilities = list(driver.get('capabilities', []))
        for capability, name in driver.get('conditional_capabilities', {}).items():
            prop = node.props.get(name)
            if prop is None or prop.type != 'boolean':
                raise ValueError(f'{node.path}: capability {capability} needs boolean {name}')
            if prop.val:
                capabilities.append(capability)
        spec = {
            'kconfig': driver.get('kconfig'),
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
                node, driver['requires']) if enabled else [],
            'init_level': driver['init_level'],
            'init_priority': driver['init_priority'],
            'has_init': driver['has_init'],
            'has_isr': driver['has_isr'],
            'interrupts': interrupts,
            'capabilities': capabilities,
        }
        specs.append(spec)
        if enabled:
            used_headers.add(driver['adapter_header'])

    identifiers = {}
    for spec in specs:
        identifier = stable_device_id(spec['path'])
        if identifier in identifiers and identifiers[identifier] != spec['path']:
            raise ValueError('stable device identifier collision')
        identifiers[identifier] = spec['path']
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
            if target is None:
                if dependency.get('lifecycle') != 'external':
                    errors.append(
                        f'{source_name} depends on '
                        f'{dependency["node"].path} via '
                        f'{dependency["reason"]}, but that dependency has '
                        f'no generated C++ lifecycle owner; mark it '
                        f'lifecycle: external only when the adapter owns it')
                continue
            if dependency.get('lifecycle') == 'external':
                errors.append(
                    f'{source_name} marks {dependency["reason"]} external, '
                    f'but {target["alias"] or target["path"]} has a '
                    f'generated C++ lifecycle owner')
                continue
            if not target['enabled']:
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
    enabled_specs = sorted(
        (spec for spec in specs if spec['enabled']),
        key=lambda spec: (
            INIT_LEVEL_ORDER[resolve_init_level_key(spec)],
            resolve_init_priority(spec),
            spec['ord']))
    if not enabled_specs:
        lines.extend([
            'const DeviceInfo *get_device_registry(size_t *count) {',
            '    if (count != nullptr) {',
            '        *count = 0;',
            '    }',
            '    return nullptr;',
            '}',
            '',
        ])
        return

    for spec in enabled_specs:
        ordinal = spec['ord']
        identifier = str_to_identifier(
            spec['alias'] or f'ord{ordinal}')
        readiness = spec.get('readiness', 'auto')
        lines.append(
            f'inline bool _check_{identifier}(const void *instance) {{')
        if readiness == 'always-ready':
            lines.extend([
                '    (void)instance;',
                '    return true;',
            ])
        else:
            lines.extend([
                f'    using Type = DeviceTrait<{ordinal}>::type;',
                '    auto *device = static_cast<const Type *>(instance);',
            ])
            if readiness == 'device-base':
                lines.extend([
                    '    static_assert(std::is_base_of_v<DeviceBase, Type>,',
                    '        "readiness=device-base requires DeviceBase");',
                    '    return device->is_ready();',
                ])
            elif readiness == 'is-initialized':
                lines.extend([
                    '    static_assert(detail::HasIsInitialized<const Type>::value,',
                    '        "readiness=is-initialized requires a const "',
                    '        "is_initialized() method");',
                    '    return device->is_initialized();',
                ])
            else:
                lines.append('    return detail::device_ready(*device);')
        lines.extend([
            '}',
            f'using _pm_type_{identifier} = '
            f'DeviceTrait<{ordinal}>::type;',
            f'static_assert(detail::HasSuspend<_pm_type_{identifier}>::value '
            f'== detail::HasResume<_pm_type_{identifier}>::value,',
            '    "device power management requires paired suspend()/resume()");',
            f'inline int _suspend_{identifier}(void *instance) {{',
            f'    using Type = DeviceTrait<{ordinal}>::type;',
            '    auto *device = static_cast<Type *>(instance);',
            '    return detail::device_suspend(*device);',
            '}',
            f'inline int _resume_{identifier}(void *instance) {{',
            f'    using Type = DeviceTrait<{ordinal}>::type;',
            '    auto *device = static_cast<Type *>(instance);',
            '    return detail::device_resume(*device);',
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
        suspend = f'_suspend_{str_to_identifier(alias)}'
        resume = f'_resume_{str_to_identifier(alias)}'
        init_priority = resolve_init_priority(spec)
        lines.append(
            f'    {{ .ord = {ordinal}, .alias = "{alias}", '
            f'.type_name = "{type_name}", '
            f'.instance = &DeviceTrait<{ordinal}>::instance, '
            f'.is_ready = {check}, .suspend = {suspend}, '
            f'.resume = {resume}, .init_priority = {init_priority}U, '
            f'.supports_power_management = '
            f'detail::HasSuspend<DeviceTrait<{ordinal}>::type>::value && '
            f'detail::HasResume<DeviceTrait<{ordinal}>::type>::value, '
            f'.stable_id = {stable_device_id(spec["path"])}U, '
            f'.capabilities = {capability_mask(spec)}U }},')

    lines.extend([
        '};',
        '',
        'const DeviceInfo *get_device_registry(size_t *count) {',
        '    if (count != nullptr) {',
        f'        *count = {len(enabled_specs)};',
        '    }',
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
        lines.extend([
            f'template <> struct DeviceMetadata<{spec["ord"]}> {{',
            f'    static constexpr uint32_t stable_id = {stable_device_id(spec["path"])}U;',
            f'    static constexpr uint32_t capabilities = {capability_mask(spec)}U;',
            '};',
        ])
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
    irq_handlers = {}
    for spec in specs:
        if not spec['enabled']:
            continue
        for interrupt in spec['interrupts']:
            irq_handlers.setdefault(interrupt['irq'], []).append(
                (spec, interrupt))

    for irq, handlers in irq_handlers.items():
        if len(handlers) > 1 and not all(
                interrupt['shared']
                for _, interrupt in handlers):
            paths = ', '.join(spec['path'] for spec, _ in handlers)
            raise ValueError(
                f'IRQ {irq} is used by multiple non-shared '
                f'devices: {paths}')

    lines = [
        '// Auto-generated by gen_device_traits.py. DO NOT EDIT.',
        '',
        '#include <drivers_generated.h>',
        '#include <init.h>',
        '#include <irq.h>',
        '#include <osal.h>',
        '',
    ]
    for irq in sorted(irq_handlers):
        lines.append(
            f'extern "C" void IRQ{irq}_Handler(void);')
    lines.extend([
        '',
        'namespace hal {',
        '',
    ])
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

        if spec['has_init'] or spec['interrupts']:
            function_name = f'_init_{safe_name}'
            rollback_name = f'_deinit_{safe_name}'
            lines.extend([
                f'static int {function_name}() {{',
            ])
            for interrupt in spec['interrupts']:
                irq = interrupt['irq']
                priority = interrupt['priority']
                if interrupt['uses_osal']:
                    lines.extend([
                        f'    static_assert(Irq::nativePriority({priority}U) >= '
                        'osal::kMinRtosCallableIrqPriority,',
                        f'                  "IRQ {irq} priority cannot '
                        'call OSAL ISR APIs");',
                    ])
                lines.extend([
                    f'    static_assert({priority}U <= '
                    'Irq::kLowestPriority,',
                    f'                  "IRQ {irq} priority exceeds the '
                    'selected SoC range");',
                ])
                lines.extend([
                    f'    Irq::disable({irq});',
                    f'    Irq::connect({irq}, IRQ{irq}_Handler);',
                    f'    Irq::setPriority({irq}, {priority}U);',
                    f'    Irq::clearPending({irq});',
                ])
            if spec['has_init']:
                if spec['interrupts']:
                    lines.extend([
                        f'    const int result = '
                        f'DeviceTrait<{ordinal}>::init();',
                        '    if (result != 0) {',
                    ])
                    for interrupt in spec['interrupts']:
                        lines.append(
                            f'        Irq::disable({interrupt["irq"]});')
                    lines.extend([
                        '        return result;',
                        '    }',
                    ])
                    for interrupt in spec['interrupts']:
                        if interrupt['enable_on_init']:
                            lines.append(
                                f'    Irq::enable({interrupt["irq"]});')
                    lines.append('    return result;')
                else:
                    lines.append(
                        f'    return DeviceTrait<{ordinal}>::init();')
            else:
                for interrupt in spec['interrupts']:
                    if interrupt['enable_on_init']:
                        lines.append(
                            f'    Irq::enable({interrupt["irq"]});')
                lines.append('    return 0;')
            lines.extend([
                '}',
                '',
                f'static int {rollback_name}() {{',
            ])
            for interrupt in spec['interrupts']:
                lines.append(f'    Irq::disable({interrupt["irq"]});')
            lines.extend([
                f'    return detail::device_deinit('
                f'DeviceTrait<{ordinal}>::instance);',
                '}',
                '',
            ])
            init_functions.append((
                function_name,
                rollback_name,
                resolve_init_level(spec),
                resolve_init_priority(spec),
            ))

    append_registry(lines, specs)
    lines.extend([
        '} // namespace hal',
        '',
    ])

    for irq in sorted(irq_handlers):
        handlers = irq_handlers[irq]
        aliases = ', '.join(
            spec['alias'] or f'ord{spec["ord"]}'
            for spec, _ in handlers)
        lines.extend([
            f'// {aliases} ISR',
            f'extern "C" void IRQ{irq}_Handler(void) {{',
            '    osal::IsrContext context;',
        ])
        for spec, interrupt in handlers:
            lines.append(
                f'    hal::DeviceTrait<{spec["ord"]}>::'
                f'{interrupt["method"]}(context);')
        lines.extend([
            '}',
            '',
        ])

    for function_name, rollback_name, level, priority in init_functions:
        lines.append(
            f'SYS_INIT_ROLLBACK(hal::{function_name}, '
            f'hal::{rollback_name}, {level}, {priority});')
    lines.append('')
    return '\n'.join(lines)


def render_report(specs, resources=None):
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
                'lifecycle': dependency.get('lifecycle', 'generated'),
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
            'backend': spec.get('kconfig'),
            'enabled': spec['enabled'],
            'init_level': resolve_init_level_key(spec),
            'init_priority': resolve_init_priority(spec),
            'readiness': spec['readiness'],
            'device_base': spec['device_base'],
            'binding': spec['binding_path'],
            'interrupts': spec['interrupts'],
            'dependencies': dependencies,
            'stable_id': stable_device_id(spec['path']),
            'capabilities': spec.get('capabilities', []),
        })

    report = {
        'schema': 'rtos-sdk.devices.v7',
        'resources': resources or [],
        'device_count': len(devices),
        'enabled_count': sum(
            1 for device in devices if device['enabled']),
        'devices': devices,
    }
    return json.dumps(
        report, ensure_ascii=False, indent=2) + '\n'


def validate_image_devices(specs, config, requirements):
    enabled_symbols = {line.removeprefix('CONFIG_').removesuffix('=y')
                       for line in config.splitlines()
                       if line.startswith('CONFIG_') and line.endswith('=y')}
    devices = {alias: spec for spec in specs for alias in spec['node'].aliases}
    for requirement in requirements:
        alias, separator, capability = requirement.partition(':')
        if not separator or capability not in CAPABILITY_BITS:
            raise ValueError(f'invalid device requirement {requirement!r}; expected alias:capability')
        spec = devices.get(alias)
        if not spec or not spec['enabled'] or capability not in spec['capabilities']:
            raise ValueError(f'{alias}: image requires an enabled device with {capability}')
    for spec in specs:
        backend = spec['kconfig']
        if spec['enabled'] and backend and backend not in enabled_symbols:
            capabilities = ', '.join(spec['capabilities']) or spec['type']
            raise ValueError(f"{spec['alias'] or spec['path']} ({spec['path']}, {capabilities}): "
                             f"enabled device requires CONFIG_{backend}=y")


def main():
    args = parse_args()
    try:
        edt = load_edt(args.edt_pickle)
        drivers = {}
        for directory in args.bindings_dir:
            drivers.update(scan_bindings(directory))
        specs, used_headers = generate_specs(edt, drivers, instantiate=not args.no_instances)
        validate_image_devices(specs, args.config.read_text(encoding='utf-8'), args.require)
        resources = collect_resources(edt)
        check_init_dependencies(specs)

        write_if_changed(
            args.header_out,
            render_header(specs, used_headers))
        write_if_changed(
            args.source_out,
            render_source(specs))
        write_if_changed(
            args.report_out,
            render_report(specs, resources))
    except (OSError, ValueError) as error:
        print(f'gen_device_traits.py: error: {error}',
              file=sys.stderr)
        return 1

    print(
        f'Generated {len(specs)} DeviceTrait entries from EDT')
    return 0


if __name__ == '__main__':
    sys.exit(main())
