#!/usr/bin/env python3
"""Strict parser for the cxx-driver adapter binding contract."""

import os
import re

import yaml

from device_codegen_common import canonical_path


DEFAULT_INIT_PRIORITY = 25
DEFAULT_INIT_LEVEL = 'pre-kernel-2'

INIT_LEVELS = {
    'early': 'INITCALL_LEVEL_EARLY',
    'pre-kernel-1': 'INITCALL_LEVEL_PRE_KERNEL_1',
    'pre-kernel-2': 'INITCALL_LEVEL_PRE_KERNEL_2',
    'pre-kernel-3': 'INITCALL_LEVEL_PRE_KERNEL_3',
    'post-kernel': 'INITCALL_LEVEL_POST_KERNEL',
    'application': 'INITCALL_LEVEL_APPLICATION',
}

ADAPTER_DRIVER_KEYS = {
    'adapter',
    'type-name',
    'init',
    'isr',
    'init-level',
    'init-priority',
    'requires',
    'scope',
    'readiness',
    'device-base',
}

ADAPTER_KEYS = {
    'header',
    'macro',
}

CPP_IDENTIFIER_RE = re.compile(r'^[A-Za-z_][A-Za-z0-9_]*$')


def normalize_init_level(value):
    if value is None:
        value = DEFAULT_INIT_LEVEL
    if not isinstance(value, str):
        raise ValueError(
            f'init-level must be a string, got {value!r}')

    key = value.strip().lower().replace('_', '-')
    if key.startswith('initcall-level-'):
        key = key[len('initcall-level-'):]

    aliases = {
        'prekernel1': 'pre-kernel-1',
        'prekernel2': 'pre-kernel-2',
        'prekernel3': 'pre-kernel-3',
        'pre-kernel1': 'pre-kernel-1',
        'pre-kernel2': 'pre-kernel-2',
        'pre-kernel3': 'pre-kernel-3',
        'postkernel': 'post-kernel',
    }
    key = aliases.get(key, key)

    if key not in INIT_LEVELS:
        valid = ', '.join(sorted(INIT_LEVELS))
        raise ValueError(
            f'unknown init-level {value!r}; expected: {valid}')
    return key


def parse_requires(raw_requires, filepath):
    if raw_requires is None:
        return []
    if not isinstance(raw_requires, list):
        raise ValueError(
            f'cxx-driver.requires in {filepath} must be a list')

    requires = []
    for item in raw_requires:
        if item == 'parent':
            requires.append(('parent', None))
            continue

        if isinstance(item, dict) and len(item) == 1:
            kind, property_name = next(iter(item.items()))
            if kind not in ('phandle', 'phandle-array'):
                raise ValueError(
                    f'unknown requires kind in {filepath}: {kind!r}')
            if not isinstance(property_name, str) or not property_name:
                raise ValueError(
                    f'invalid requires property in {filepath}: '
                    f'{property_name!r}')
            normalized = kind.replace('-', '_')
            requires.append((normalized, property_name))
            continue

        raise ValueError(
            f'invalid requires entry in {filepath}: {item!r}')
    return requires


def parse_adapter(cxx_driver, filepath, compatible):
    if not isinstance(cxx_driver, dict):
        raise ValueError(
            f'cxx-driver in {filepath} must be a mapping')

    unknown = set(cxx_driver) - ADAPTER_DRIVER_KEYS
    if unknown:
        names = ', '.join(sorted(unknown))
        raise ValueError(
            f'unknown cxx-driver keys in {filepath}: {names}')

    adapter = cxx_driver.get('adapter')
    if not isinstance(adapter, dict):
        raise ValueError(
            f'{filepath}: legacy cxx-driver DSL is unsupported; '
            f'use an adapter mapping')

    unknown_adapter = set(adapter) - ADAPTER_KEYS
    if unknown_adapter:
        names = ', '.join(sorted(unknown_adapter))
        raise ValueError(
            f'unknown adapter keys in {filepath}: {names}')

    header = adapter.get('header')
    macro = adapter.get('macro')
    type_name = cxx_driver.get('type-name')
    if not isinstance(header, str) or not header.strip():
        raise ValueError(f'invalid adapter header in {filepath}')
    if not isinstance(macro, str) or not CPP_IDENTIFIER_RE.fullmatch(macro):
        raise ValueError(
            f'invalid adapter macro in {filepath}: {macro!r}')
    if not isinstance(type_name, str) or not type_name.strip():
        raise ValueError(f'invalid type-name in {filepath}')

    has_init = cxx_driver.get('init', False)
    has_isr = cxx_driver.get('isr', False)
    device_base = cxx_driver.get('device-base', False)
    for key, value in (
            ('init', has_init),
            ('isr', has_isr),
            ('device-base', device_base)):
        if not isinstance(value, bool):
            raise ValueError(
                f'cxx-driver.{key} in {filepath} must be boolean')

    scope = cxx_driver.get('scope', 'node')
    if scope not in ('node', 'children'):
        raise ValueError(
            f'cxx-driver.scope in {filepath} must be '
            f'"node" or "children"')

    readiness = cxx_driver.get(
        'readiness', 'device-base' if device_base else 'auto')
    if readiness not in (
            'auto', 'device-base', 'is-initialized', 'always-ready'):
        raise ValueError(
            f'invalid readiness in {filepath}: {readiness!r}')

    init_level = normalize_init_level(
        cxx_driver.get('init-level', DEFAULT_INIT_LEVEL))
    init_priority = cxx_driver.get(
        'init-priority', DEFAULT_INIT_PRIORITY)
    try:
        init_priority = int(init_priority)
    except (TypeError, ValueError) as error:
        raise ValueError(
            f'invalid init-priority in {filepath}: '
            f'{init_priority!r}') from error
    if not 0 <= init_priority <= 999:
        raise ValueError(
            f'init-priority out of range in {filepath}: '
            f'{init_priority}')

    return {
        'adapter_header': header.strip(),
        'adapter_macro': macro,
        'type_name': type_name.strip(),
        'compatible': compatible,
        'has_init': has_init,
        'has_isr': has_isr,
        'scope': scope,
        'init_level': init_level,
        'init_priority': init_priority,
        'requires': parse_requires(
            cxx_driver.get('requires', []), filepath),
        'readiness': readiness,
        'device_base': device_base,
        'binding_path': filepath,
    }


def scan_bindings(bindings_dir):
    drivers = {}
    for root, dirs, files in os.walk(bindings_dir):
        dirs.sort()
        for filename in sorted(files):
            if not filename.endswith('.yaml'):
                continue

            filepath = os.path.join(root, filename)
            with open(filepath, 'r', encoding='utf-8') as binding_file:
                data = yaml.safe_load(binding_file)
            if not isinstance(data, dict) or 'cxx-driver' not in data:
                continue

            compatible = data.get('compatible')
            if not isinstance(compatible, str) or not compatible:
                raise ValueError(
                    f'{filepath}: cxx-driver requires compatible')

            key = canonical_path(filepath)
            drivers[key] = parse_adapter(
                data['cxx-driver'], filepath, compatible)
    return drivers


def driver_for_node(node, drivers):
    binding_path = getattr(node, 'binding_path', None)
    if not binding_path:
        return None

    driver = drivers.get(canonical_path(binding_path))
    if driver is None:
        return None

    if driver['scope'] == 'node':
        if node.matching_compat != driver['compatible']:
            return None
        return driver

    parent = node.parent
    if parent is None or not parent.binding_path:
        return None
    if canonical_path(parent.binding_path) != canonical_path(binding_path):
        return None
    return driver
