#!/usr/bin/env python3
"""Shared helpers for EDT-based source generators."""

import os
import pickle
import re
import sys


_SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
_EDTLIB_DIR = os.path.join(
    _SCRIPT_DIR, 'dts', 'python-devicetree', 'src')
if _EDTLIB_DIR not in sys.path:
    sys.path.insert(0, _EDTLIB_DIR)

from devicetree import edtlib  # noqa: E402


def load_edt(path):
    """Load a trusted build-local edtlib.EDT pickle."""
    with open(path, 'rb') as edt_file:
        edt = pickle.load(edt_file)

    if not isinstance(edt, edtlib.EDT):
        raise ValueError(
            f'{path} does not contain an edtlib.EDT object')
    return edt


def str_to_identifier(value):
    """Match gen_defines.py identifier normalization."""
    return re.sub(r'[-,.@/+]', '_', value.lower())


def node_identifier(node):
    """Return the public DT macro identifier for an edtlib node."""
    components = ['N']
    if node.parent is not None:
        components.extend(
            f'S_{str_to_identifier(component)}'
            for component in node.path.split('/')[1:])
    return 'DT_' + '_'.join(components)


def node_alias(node):
    """Return the first alias, matching the generated device API."""
    return (
        str_to_identifier(node.aliases[0])
        if node.aliases else '')


def node_enabled(node):
    return node.status == 'okay'


def canonical_path(path):
    return os.path.normcase(
        os.path.normpath(os.path.abspath(path)))


def write_if_changed(path, content):
    """Atomically replace path only when its content changed."""
    directory = os.path.dirname(os.path.abspath(path))
    os.makedirs(directory, exist_ok=True)

    try:
        with open(path, 'r', encoding='utf-8') as current:
            if current.read() == content:
                return False
    except FileNotFoundError:
        pass

    temporary = f'{path}.tmp'
    with open(temporary, 'w', encoding='utf-8', newline='\n') as output:
        output.write(content)
    os.replace(temporary, path)
    return True
