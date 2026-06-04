# Copyright (c) 2018-2019 Linaro
# Copyright (c) 2019 Nordic Semiconductor ASA
#
# SPDX-License-Identifier: Apache-2.0

import functools
import inspect
import operator
import os
import pickle
import re
import sys
from pathlib import Path

import yaml

ZEPHYR_BASE = str(Path(__file__).resolve().parents[2])
sys.path.insert(0, os.path.join(ZEPHYR_BASE, "scripts", "dts",
                                "python-devicetree", "src"))

# Types we support
# 'string', 'int', 'hex', 'bool'

doc_mode = os.environ.get('KCONFIG_DOC_MODE') == "1"
soc_yaml_file = os.environ.get("SOC_YAML_FILE")

if not doc_mode:
    EDT_PICKLE = os.environ.get("EDT_PICKLE")

    # The "if" handles a missing dts.
    if EDT_PICKLE is not None and os.path.isfile(EDT_PICKLE):
        with open(EDT_PICKLE, 'rb') as f:
            edt = pickle.load(f)
            edtlib = inspect.getmodule(edt)
    else:
        edt = None
        edtlib = None


def _warn(kconf, msg):
    print("{}:{}: WARNING: {}".format(kconf.filename, kconf.linenr, msg))


def _dt_units_to_scale(unit):
    if not unit:
        return 0
    if unit in {'k', 'K'}:
        return 10
    if unit in {'m', 'M'}:
        return 20
    if unit in {'g', 'G'}:
        return 30
    if unit in {'kb', 'Kb'}:
        return 13
    if unit in {'mb', 'Mb'}:
        return 23
    if unit in {'gb', 'Gb'}:
        return 33


def dt_chosen_label(kconf, _, chosen):
    """
    This function takes a 'chosen' property and treats that property as a path
    to an EDT node.  If it finds an EDT node, it will look to see if that node
    has a "label" property and return the value of that "label". If not, we
    return the node's name in the devicetree.
    """
    if doc_mode or edt is None:
        return ""

    node = edt.chosen_node(chosen)
    if not node:
        return ""

    if "label" not in node.props:
        return node.name

    return node.props["label"].val


def dt_chosen_enabled(kconf, _, chosen):
    """
    This function returns "y" if /chosen contains a property named 'chosen'
    that points to an enabled node, and "n" otherwise
    """
    if doc_mode or edt is None:
        return "n"

    node = edt.chosen_node(chosen)
    return "y" if node and node.status == "okay" else "n"


def dt_chosen_path(kconf, _, chosen):
    """
    This function takes a /chosen node property and returns the path
    to the node in the property value, or the empty string.
    """
    if doc_mode or edt is None:
        return "n"

    node = edt.chosen_node(chosen)

    return node.path if node else ""

def dt_chosen_has_compat(kconf, _, chosen, compat):
    """
    This function takes a /chosen node property and returns 'y' if the
    chosen node has the provided compatible string 'compat'
    """
    if doc_mode or edt is None:
        return "n"

    node = edt.chosen_node(chosen)

    if node is None:
        return "n"

    if compat in node.compats:
        return "y"

    return "n"

def dt_node_enabled(kconf, name, node):
    """
    This function is used to test if a node is enabled (has status
    'okay') or not.

    The 'node' argument is a string which is either a path or an
    alias, or both, depending on 'name'.

    If 'name' is 'dt_path_enabled', 'node' is an alias or a path. If
    'name' is 'dt_alias_enabled, 'node' is an alias.
    """

    if doc_mode or edt is None:
        return "n"

    if name == "dt_alias_enabled":
        if node.startswith("/"):
            # EDT.get_node() works with either aliases or paths. If we
            # are specifically being asked about an alias, reject paths.
            return "n"
    else:
        # Make sure this is being called appropriately.
        assert name == "dt_path_enabled"

    try:
        node = edt.get_node(node)
    except edtlib.EDTError:
        return "n"

    return "y" if node and node.status == "okay" else "n"


def dt_nodelabel_exists(kconf, _, label):
    """
    This function returns "y" if a nodelabel exists and "n" otherwise.
    """
    if doc_mode or edt is None:
        return "n"

    node = edt.label2node.get(label)

    return "y" if node else "n"


def dt_nodelabel_enabled(kconf, _, label):
    """
    This function is like dt_node_enabled(), but the 'label' argument
    should be a node label, like "foo" is here:

       foo: some-node { ... };
    """
    if doc_mode or edt is None:
        return "n"

    node = edt.label2node.get(label)

    return "y" if node and node.status == "okay" else "n"


def _node_reg_addr(node, index, unit):
    if not node:
        return 0

    if not node.regs:
        return 0

    if int(index) >= len(node.regs):
        return 0

    if node.regs[int(index)].addr is None:
        return 0

    return node.regs[int(index)].addr >> _dt_units_to_scale(unit)


def _node_reg_size(node, index, unit):
    if not node:
        return 0

    if not node.regs:
        return 0

    if int(index) >= len(node.regs):
        return 0

    if node.regs[int(index)].size is None:
        return 0

    return node.regs[int(index)].size >> _dt_units_to_scale(unit)


def _node_int_prop(node, prop, unit=None):
    """
    This function takes a 'node' and  will look to see if that 'node' has a
    property called 'prop' and if that 'prop' is an integer type will return
    the value of the property 'prop' as either a string int or string hex
    value, if not we return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
        'kb' or 'Kb'  divide by 8192 (1 << 13)
        'mb' or 'Mb'  divide by 8,388,608 (1 << 23)
        'gb' or 'Gb'  divide by 8,589,934,592 (1 << 33)
    """
    if not node:
        return 0

    if prop not in node.props:
        return 0

    if node.props[prop].type != "int":
        return 0

    return node.props[prop].val >> _dt_units_to_scale(unit)


def _node_array_prop(node, prop, index=0, unit=None):
    """
    This function takes a 'node' and  will look to see if that 'node' has a
    property called 'prop' and if that 'prop' is an array type will return
    the value of the property 'prop' at the given 'index' as either a string int
    or string hex value. If the property 'prop' is not found or the given 'index'
    is out of range it will return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
    """
    if not node:
        return 0

    if prop not in node.props:
        return 0
    if node.props[prop].type != "array":
        return 0
    if int(index) >= len(node.props[prop].val):
        return 0
    return node.props[prop].val[int(index)] >> _dt_units_to_scale(unit)

def _node_ph_array_prop(node, prop, index, cell, unit=None):
    """
    This function takes a 'node', a property name ('prop'), index ('index') and
    a cell ('cell') and it will look to see if that node has a property
    called 'prop' and if that 'prop' is an phandle-array type.
    Then it will check if that phandle array has a cell matching the given index
    and then return the value of the cell named 'cell' in this array index.
    If not found it will return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
    """
    if not node:
        return 0

    if prop not in node.props:
        return 0
    if node.props[prop].type != "phandle-array":
        return 0
    if int(index) >= len(node.props[prop].val):
        return 0
    if cell not in node.props[prop].val[int(index)].data.keys():
        return 0
    return node.props[prop].val[int(index)].data[cell] >> _dt_units_to_scale(unit)

def _dt_chosen_reg_addr(kconf, chosen, index=0, unit=None):
    """
    This function takes a 'chosen' property and treats that property as a path
    to an EDT node.  If it finds an EDT node, it will look to see if that
    node has a register at the given 'index' and return the address value of
    that reg, if not we return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
        'kb' or 'Kb'  divide by 8192 (1 << 13)
        'mb' or 'Mb'  divide by 8,388,608 (1 << 23)
        'gb' or 'Gb'  divide by 8,589,934,592 (1 << 33)
    """
    if doc_mode or edt is None:
        return 0

    node = edt.chosen_node(chosen)

    return _node_reg_addr(node, index, unit)


def _dt_chosen_reg_size(kconf, chosen, index=0, unit=None):
    """
    This function takes a 'chosen' property and treats that property as a path
    to an EDT node.  If it finds an EDT node, it will look to see if that node
    has a register at the given 'index' and return the size value of that reg,
    if not we return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
        'kb' or 'Kb'  divide by 8192 (1 << 13)
        'mb' or 'Mb'  divide by 8,388,608 (1 << 23)
        'gb' or 'Gb'  divide by 8,589,934,592 (1 << 33)
    """
    if doc_mode or edt is None:
        return 0

    node = edt.chosen_node(chosen)

    return _node_reg_size(node, index, unit)


def dt_chosen_reg(kconf, name, chosen, index=0, unit=None):
    """
    This function just routes to the proper function and converts
    the result to either a string int or string hex value.
    """
    if name == "dt_chosen_reg_size_int":
        return str(_dt_chosen_reg_size(kconf, chosen, index, unit))
    if name == "dt_chosen_reg_size_hex":
        return hex(_dt_chosen_reg_size(kconf, chosen, index, unit))
    if name == "dt_chosen_reg_addr_int":
        return str(_dt_chosen_reg_addr(kconf, chosen, index, unit))
    if name == "dt_chosen_reg_addr_hex":
        return hex(_dt_chosen_reg_addr(kconf, chosen, index, unit))


def _dt_chosen_partition_addr(kconf, chosen, index=0, unit=None):
    """
    This function takes a 'chosen' property and treats that property as a path
    to an EDT node.  If it finds an EDT node, it will look to see if that
    node has a register, and if that node has a grandparent that has a register
    at the given 'index'. The addition of both addresses will be returned, if
    not, we return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
        'kb' or 'Kb'  divide by 8192 (1 << 13)
        'mb' or 'Mb'  divide by 8,388,608 (1 << 23)
        'gb' or 'Gb'  divide by 8,589,934,592 (1 << 33)
    """
    if doc_mode or edt is None:
        return 0

    node = edt.chosen_node(chosen)
    if not node:
        return 0

    p_node = node.parent
    if not p_node:
        return 0

    return _node_reg_addr(p_node.parent, index, unit) + _node_reg_addr(node, 0, unit)


def dt_chosen_partition_addr(kconf, name, chosen, index=0, unit=None):
    """
    This function just routes to the proper function and converts
    the result to either a string int or string hex value.
    """
    if name == "dt_chosen_partition_addr_int":
        return str(_dt_chosen_partition_addr(kconf, chosen, index, unit))
    if name == "dt_chosen_partition_addr_hex":
        return hex(_dt_chosen_partition_addr(kconf, chosen, index, unit))


def _dt_node_reg_addr(kconf, path, index=0, unit=None):
    """
    This function takes a 'path' and looks for an EDT node at that path. If it
    finds an EDT node, it will look to see if that node has a register at the
    given 'index' and return the address value of that reg, if not we return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
        'kb' or 'Kb'  divide by 8192 (1 << 13)
        'mb' or 'Mb'  divide by 8,388,608 (1 << 23)
        'gb' or 'Gb'  divide by 8,589,934,592 (1 << 33)
    """
    if doc_mode or edt is None:
        return 0

    try:
        node = edt.get_node(path)
    except edtlib.EDTError:
        return 0

    return _node_reg_addr(node, index, unit)


def _dt_node_reg_size(kconf, path, index=0, unit=None):
    """
    This function takes a 'path' and looks for an EDT node at that path. If it
    finds an EDT node, it will look to see if that node has a register at the
    given 'index' and return the size value of that reg, if not we return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
        'kb' or 'Kb'  divide by 8192 (1 << 13)
        'mb' or 'Mb'  divide by 8,388,608 (1 << 23)
        'gb' or 'Gb'  divide by 8,589,934,592 (1 << 33)
    """
    if doc_mode or edt is None:
        return 0

    try:
        node = edt.get_node(path)
    except edtlib.EDTError:
        return 0

    return _node_reg_size(node, index, unit)


def dt_node_reg(kconf, name, path, index=0, unit=None):
    """
    This function just routes to the proper function and converts
    the result to either a string int or string hex value.
    """
    if name == "dt_node_reg_size_int":
        return str(_dt_node_reg_size(kconf, path, index, unit))
    if name == "dt_node_reg_size_hex":
        return hex(_dt_node_reg_size(kconf, path, index, unit))
    if name == "dt_node_reg_addr_int":
        return str(_dt_node_reg_addr(kconf, path, index, unit))
    if name == "dt_node_reg_addr_hex":
        return hex(_dt_node_reg_addr(kconf, path, index, unit))

def dt_nodelabel_reg(kconf, name, label, index=0, unit=None):
    """
    This function is like dt_node_reg(), but the 'label' argument
    should be a node label, like "foo" is here:

       foo: some-node { ... };
    """
    if doc_mode or edt is None:
        node = None
    else:
        node = edt.label2node.get(label)

    if name == "dt_nodelabel_reg_size_int":
        return str(_dt_node_reg_size(kconf, node.path, index, unit)) if node else "0"
    if name == "dt_nodelabel_reg_size_hex":
        return hex(_dt_node_reg_size(kconf, node.path, index, unit)) if node else "0x0"
    if name == "dt_nodelabel_reg_addr_int":
        return str(_dt_node_reg_addr(kconf, node.path, index, unit)) if node else "0"
    if name == "dt_nodelabel_reg_addr_hex":
        return hex(_dt_node_reg_addr(kconf, node.path, index, unit)) if node else "0x0"


def _dt_node_bool_prop_generic(node_search_function, search_arg, prop):
    """
    This function takes the 'node_search_function' and uses it to search for
    a node with 'search_arg' and if node exists, checks if 'prop' exists
    inside the node and is a boolean, if it is true, returns "y".
    Otherwise, it returns "n".
    """
    try:
        node = node_search_function(search_arg)
    except edtlib.EDTError:
        return "n"

    if node is None:
        return "n"

    if prop not in node.props:
        return "n"

    if node.props[prop].type != "boolean":
        return "n"

    if node.props[prop].val:
        return "y"

    return "n"

def dt_node_bool_prop(kconf, _, path, prop):
    """
    This function takes a 'path' and looks for an EDT node at that path. If it
    finds an EDT node, it will look to see if that node has a boolean property
    by the name of 'prop'.  If the 'prop' exists it will return "y" otherwise
    we return "n".
    """
    if doc_mode or edt is None:
        return "n"

    return _dt_node_bool_prop_generic(edt.get_node, path, prop)

def dt_nodelabel_bool_prop(kconf, _, label, prop):
    """
    This function takes a 'label' and looks for an EDT node with that label.
    If it finds an EDT node, it will look to see if that node has a boolean
    property by the name of 'prop'.  If the 'prop' exists it will return "y"
    otherwise we return "n".
    """
    if doc_mode or edt is None:
        return "n"

    return _dt_node_bool_prop_generic(edt.label2node.get, label, prop)

def dt_nodelabel_int_prop(kconf, _, label, prop):
    """
    This function takes a 'label' and looks for an EDT node with that label.
    If it finds an EDT node, it will look to see if that node has a int
    property by the name of 'prop'.  If the 'prop' exists it will return the
    value of the property, otherwise it returns "0".
    """
    if doc_mode or edt is None:
        return "0"

    try:
        node = edt.label2node.get(label)
    except edtlib.EDTError:
        return "0"

    if not node or node.props[prop].type != "int":
        return "0"

    if not node.props[prop].val:
        return "0"

    return str(node.props[prop].val)

def dt_chosen_bool_prop(kconf, _, chosen, prop):
    """
    This function takes a /chosen node property named 'chosen', and
    looks for the chosen node. If that node exists and has a boolean
    property 'prop', it returns "y". Otherwise, it returns "n".
    """
    if doc_mode or edt is None:
        return "n"

    return _dt_node_bool_prop_generic(edt.chosen_node, chosen, prop)

def _dt_node_has_prop_generic(node_search_function, search_arg, prop):
    """
    This function takes the 'node_search_function' and uses it to search for
    a node with 'search_arg' and if node exists, then checks if 'prop'
    exists inside the node and returns "y". Otherwise, it returns "n".
    """
    try:
        node = node_search_function(search_arg)
    except edtlib.EDTError:
        return "n"

    if node is None:
        return "n"

    if prop in node.props:
        return "y"

    return "n"

def dt_node_has_prop(kconf, _, path, prop):
    """
    This function takes a 'path' and looks for an EDT node at that path. If it
    finds an EDT node, it will look to see if that node has a property
    by the name of 'prop'.  If the 'prop' exists it will return "y" otherwise
    it returns "n".
    """
    if doc_mode or edt is None:
        return "n"

    return _dt_node_has_prop_generic(edt.get_node, path, prop)

def dt_nodelabel_has_prop(kconf, _, label, prop):
    """
    This function takes a 'label' and looks for an EDT node with that label.
    If it finds an EDT node, it will look to see if that node has a property
    by the name of 'prop'.  If the 'prop' exists it will return "y" otherwise
    it returns "n".
    """
    if doc_mode or edt is None:
        return "n"

    return _dt_node_has_prop_generic(edt.label2node.get, label, prop)

def dt_node_int_prop(kconf, name, path, prop, unit=None):
    """
    This function takes a 'path' and property name ('prop') looks for an EDT
    node at that path. If it finds an EDT node, it will look to see if that
    node has a property called 'prop' and if that 'prop' is an integer type
    will return the value of the property 'prop' as either a string int or
    string hex value, if not we return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
        'kb' or 'Kb'  divide by 8192 (1 << 13)
        'mb' or 'Mb'  divide by 8,388,608 (1 << 23)
        'gb' or 'Gb'  divide by 8,589,934,592 (1 << 33)
    """
    if doc_mode or edt is None:
        return "0"

    try:
        node = edt.get_node(path)
    except edtlib.EDTError:
        return "0"

    if name == "dt_node_int_prop_int":
        return str(_node_int_prop(node, prop, unit))
    if name == "dt_node_int_prop_hex":
        return hex(_node_int_prop(node, prop, unit))


def dt_node_array_prop(kconf, name, path, prop, index, unit=None):
    """
    This function takes a 'path', property name ('prop') and index ('index')
    and looks for an EDT node at that path. If it finds an EDT node, it will
    look to see if that node has a property called 'prop' and if that 'prop'
    is an array type will return the value of the property 'prop' at the given
    'index' as either a string int or string hex value. If not found we return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
    """
    if doc_mode or edt is None:
        return "0"

    try:
        node = edt.get_node(path)
    except edtlib.EDTError:
        return "0"
    if name == "dt_node_array_prop_int":
        return str(_node_array_prop(node, prop, index, unit))
    if name == "dt_node_array_prop_hex":
        return hex(_node_array_prop(node, prop, index, unit))


def dt_node_ph_array_prop(kconf, name, path, prop, index, cell, unit=None):
    """
    This function takes a 'path', property name ('prop'), index ('index') and
    a cell ('cell') and looks for an EDT node at that path.
    If it finds an EDT node, it will look to see if that node has a property
    called 'prop' and if that 'prop' is an phandle-array type.
    Then it will check if that phandle array has a cell matching the given index
    and ten return the value of the cell named 'cell' in this array index as
    either a string int or string hex value. If not found we return 0.

    The function will divide the value based on 'unit':
        None        No division
        'k' or 'K'  divide by 1024 (1 << 10)
        'm' or 'M'  divide by 1,048,576 (1 << 20)
        'g' or 'G'  divide by 1,073,741,824 (1 << 30)
    """
    if doc_mode or edt is None:
        return "0"

    try:
        node = edt.get_node(path)
    except edtlib.EDTError:
        return "0"
    if name == "dt_node_ph_array_prop_int":
        return str(_node_ph_array_prop(node, prop, index, cell, unit))
    if name == "dt_node_ph_array_prop_hex":
        return hex(_node_ph_array_prop(node, prop, index, cell, unit))

def dt_node_ph_prop_path(kconf, name, path, prop):
    """
    This function takes a 'path' and a property name ('prop') and
    looks for an EDT node at that path. If it finds an EDT node,
    it will look to see if that node has a property called 'prop'
    and if that 'prop' is an phandle type. Then it will return the
    path to the pointed-to node, or an empty string if there is
    no such node.
    """
    if doc_mode or edt is None:
        return ""

    try:
        node = edt.get_node(path)
    except edtlib.EDTError:
        return ""

    if prop not in node.props:
        return ""
    if node.props[prop].type != "phandle":
        return ""

    phandle = node.props[prop].val

    return phandle.path if phandle else ""

def dt_node_str_prop_equals(kconf, _, path, prop, val):
    """
    This function takes a 'path' and property name ('prop') looks for an EDT
    node at that path. If it finds an EDT node, it will look to see if that
    node has a property 'prop' of type string. If that 'prop' is equal to 'val'
    it will return "y" otherwise return "n".
    """

    if doc_mode or edt is None:
        return "n"

    try:
        node = edt.get_node(path)
    except edtlib.EDTError:
        return "n"

    if prop not in node.props:
        return "n"

    if node.props[prop].type != "string":
        return "n"

    if node.props[prop].val == val:
        return "y"

    return "n"


def dt_has_compat(kconf, _, compat):
    """
    This function takes a 'compat' and returns "y" if any compatible node
    can be found in the EDT, otherwise it returns "n".
    """
    if doc_mode or edt is None:
        return "n"

    return "y" if compat in edt.compat2nodes else "n"


def dt_compat_enabled(kconf, _, compat):
    """
    This function takes a 'compat' and returns "y" if we find a status "okay"
    compatible node in the EDT otherwise we return "n"
    """
    if doc_mode or edt is None:
        return "n"

    return "y" if compat in edt.compat2okay else "n"


def dt_compat_enabled_num(kconf, _, compat):
    """
    This function takes a 'compat' and the returns number of status "okay"
    compatible nodes in the EDT.
    """
    if doc_mode or edt is None:
        return "0"

    return str(len(edt.compat2okay[compat]))


def dt_compat_on_bus(kconf, _, compat, bus):
    """
    This function takes a 'compat' and returns "y" if we find an enabled
    compatible node in the EDT which is on bus 'bus'. It returns "n" otherwise.
    """
    if doc_mode or edt is None:
        return "n"

    if compat in edt.compat2okay:
        for node in edt.compat2okay[compat]:
            if node.on_buses is not None and bus in node.on_buses:
                return "y"

    return "n"

def dt_compat_any_has_prop(kconf, _, compat, prop, value=None):
    """
    This function takes a 'compat', a 'prop', and a 'value'.
    If value=None, the function returns "y" if any
    enabled node with compatible 'compat' also has a valid property 'prop'.
    If value is given, the function returns "y" if any enabled node with compatible 'compat'
    also has a valid property 'prop' with value 'value'.
    It returns "n" otherwise.
    """
    if doc_mode or edt is None:
        return "n"

    if compat in edt.compat2okay:
        for node in edt.compat2okay[compat]:
            if prop in node.props:
                if value is None:
                    return "y"
                elif str(node.props[prop].val) == value:
                    return "y"
    return "n"

def dt_compat_any_not_has_prop(kconf, _, compat, prop):
    """
    This function takes a 'compat', and a 'prop'.
    The function returns "y" if any enabled node with compatible 'compat'
    does NOT contain the property 'prop'.
    It returns "n" otherwise.
    """
    if doc_mode or edt is None:
        return "n"

    if compat in edt.compat2okay:
        for node in edt.compat2okay[compat]:
            if prop not in node.props:
                return "y"

    return "n"

def dt_nodelabel_has_compat(kconf, _, label, compat):
    """
    This function takes a 'label' and looks for an EDT node with that label.
    If it finds such node, it returns "y" if this node is compatible with
    the provided 'compat'. Otherwise, it return "n" .
    """
    if doc_mode or edt is None:
        return "n"

    node = edt.label2node.get(label)

    if node and compat in node.compats:
        return "y"

    return "n"

def dt_node_has_compat(kconf, _, path, compat):
    """
    This function takes a 'path' and looks for an EDT node at that path. If it
    finds an EDT node, it returns "y" if this node is compatible with
    the provided 'compat'. Otherwise, it return "n" .
    """

    if doc_mode or edt is None:
        return "n"

    try:
        node = edt.get_node(path)
    except edtlib.EDTError:
        return "n"

    if node and compat in node.compats:
        return "y"

    return "n"

def dt_nodelabel_enabled_with_compat(kconf, _, label, compat):
    """
    This function takes a 'label' and returns "y" if an enabled node with
    such label can be found in the EDT and that node is compatible with the
    provided 'compat', otherwise it returns "n".
    """
    if doc_mode or edt is None:
        return "n"

    if compat in edt.compat2okay:
        for node in edt.compat2okay[compat]:
            if label in node.labels:
                return "y"

    return "n"


def dt_nodelabel_array_prop_has_val(kconf, _, label, prop, val):
    """
    This function looks for a node with node label 'label'.
    If the node exists, it checks if the node node has a property
    'prop' with type "array". If so, and the property contains
    an element equal to the integer 'val', it returns "y".
    Otherwise, it returns "n".
    """
    if doc_mode or edt is None:
        return "n"

    node = edt.label2node.get(label)

    if not node or (prop not in node.props) or (node.props[prop].type != "array"):
        return "n"
    else:
        return "y" if int(val, base=0) in node.props[prop].val else "n"


def dt_nodelabel_path(kconf, _, label):
    """
    This function takes a node label (not a label property) and
    returns the path to the node which has that label, or an empty
    string if there is no such node.
    """
    if doc_mode or edt is None:
        return ""

    node = edt.label2node.get(label)

    return node.path if node else ""

def dt_node_parent(kconf, _, path):
    """
    This function takes a 'path' and looks for an EDT node at that path. If it
    finds an EDT node, it will look for the parent of that node. If the parent
    exists, it will return the path to that parent. Otherwise, an empty string
    will be returned.
    """
    if doc_mode or edt is None:
        return ""

    try:
        node = edt.get_node(path)
    except edtlib.EDTError:
        return ""

    if node is None:
        return ""

    return node.parent.path if node.parent else ""

def dt_gpio_hogs_enabled(kconf, _):
    """
    Return "y" if any GPIO hog node is enabled. Otherwise, return "n".
    """
    if doc_mode or edt is None:
        return "n"

    for node in edt.nodes:
        if node.gpio_hogs and node.status == "okay":
            return "y"

    return "n"


def normalize_upper(kconf, _, string):
    """
    Normalize the string, so that the string only contains alpha-numeric
    characters or underscores. All non-alpha-numeric characters are replaced
    with an underscore, '_'.
    When string has been normalized it will be converted into upper case.
    """
    return re.sub(r'[^a-zA-Z0-9_]', '_', string).upper()


def shields_list_contains(kconf, _, shield):
    """
    Return "n" if cmake environment variable 'SHIELD_AS_LIST' doesn't exist.
    Return "y" if 'shield' is present list obtained after 'SHIELD_AS_LIST'
    has been split using ";" as a separator and "n" otherwise.
    """
    try:
        list = os.environ['SHIELD_AS_LIST']
    except KeyError:
        return "n"

    return "y" if shield in list.split(";") else "n"


def substring(kconf, _, string, start, stop=None):
    """
    Extracts a portion of the string, removing characters from the front, back or both.
    """
    if stop is not None:
        return string[int(start):int(stop)]
    else:
        return string[int(start):]

def arith(kconf, name, *args):
    """
    The arithmetic operations on integers.
    If three or more arguments are given, it returns the result of performing
    the operation on the first two arguments and operates the same operation as
    the result and the following argument.
    For interoperability with inc and dec,
    each argument can be a single number or a comma-separated list of numbers,
    but all numbers are processed as if they were individual arguments.

    Examples in Kconfig:

    $(add, 10, 3)          # -> 13
    $(add, 10, 3, 2)       # -> 15
    $(sub, 10, 3)          # -> 7
    $(sub, 10, 3, 2)       # -> 5
    $(mul, 10, 3)          # -> 30
    $(mul, 10, 3, 2)       # -> 60
    $(div, 10, 3)          # -> 3
    $(div, 10, 3, 2)       # -> 1
    $(mod, 10, 3)          # -> 1
    $(mod, 10, 3, 2)       # -> 1
    $(inc, 1)              # -> 2
    $(inc, 1, 1)           # -> "2,2"
    $(inc, $(inc, 1, 1))   # -> "3,3"
    $(dec, 1)              # -> 0
    $(dec, 1, 1)           # -> "0,0"
    $(dec, $(dec, 1, 1))   # -> "-1,-1"
    $(add, $(inc, 1, 1))   # -> 4
    $(div, $(dec, 1, 1))   # Error (0 div 0)
    """

    intarray = (int(val, base=0) for arg in args for val in arg.split(","))

    if name == "add":
        return str(int(functools.reduce(operator.add, intarray)))
    elif name == "add_hex":
        return hex(int(functools.reduce(operator.add, intarray)))
    elif name == "sub":
        return str(int(functools.reduce(operator.sub, intarray)))
    elif name == "sub_hex":
        return hex(int(functools.reduce(operator.sub, intarray)))
    elif name == "mul":
        return str(int(functools.reduce(operator.mul, intarray)))
    elif name == "mul_hex":
        return hex(int(functools.reduce(operator.mul, intarray)))
    elif name == "div":
        return str(int(functools.reduce(operator.truediv, intarray)))
    elif name == "div_hex":
        return hex(int(functools.reduce(operator.truediv, intarray)))
    elif name == "mod":
        return str(int(functools.reduce(operator.mod, intarray)))
    elif name == "mod_hex":
        return hex(int(functools.reduce(operator.mod, intarray)))
    elif name == "max":
        return str(int(functools.reduce(max, intarray)))
    elif name == "max_hex":
        return hex(int(functools.reduce(max, intarray)))
    elif name == "min":
        return str(int(functools.reduce(min, intarray)))
    elif name == "min_hex":
        return hex(int(functools.reduce(min, intarray)))
    else:
        assert False


def inc_dec(kconf, name, *args):
    """
    Calculate the increment and the decrement of integer sequence.
    Returns a string that concatenates numbers with a comma as a separator.
    """

    intarray = (int(val, base=0) for arg in args for val in arg.split(","))

    if name == "inc":
        return ",".join(map(lambda a: str(a + 1), intarray))
    if name == "inc_hex":
        return ",".join(map(lambda a: hex(a + 1), intarray))
    elif name == "dec":
        return ",".join(map(lambda a: str(a - 1), intarray))
    elif name == "dec_hex":
        return ",".join(map(lambda a: hex(a - 1), intarray))
    else:
        assert False


def str_is_equal(kconf, _, string1, string2):
    """
    Return "y" if string1 equals string2. Otherwise, return "n".
    """
    return "y" if string1 == string2 else "n"


def find_soc_in_yaml(soc_name_input, yaml_data):
    """
    在YAML数据中查找SOC信息，支持模糊匹配并忽略大小写

    Args:
        soc_name_input: 输入的SOC名称
        yaml_data: 加载的YAML数据

    Returns:
        tuple: (family, series, exact_soc_name) 或 None
    """
    # 将输入转换为小写用于比较
    soc_name_input_lower = soc_name_input.lower()

    exact_match = None
    partial_match = None

    if 'family' in yaml_data:
        for family in yaml_data['family']:
            family_name = family.get('name', '')
            if 'series' in family:
                for series in family['series']:
                    series_name = series.get('name', '')
                    if 'socs' in series:
                        for soc in series['socs']:
                            soc_name = soc.get('name', '')
                            soc_name_lower = soc_name.lower()

                            # 精确匹配（忽略大小写）
                            if soc_name_lower == soc_name_input_lower:
                                return family_name, series_name, soc_name

                            # 部分匹配（忽略大小写，处理名称变体）
                            # 移除尾部的'x'字符进行更宽松的匹配
                            base_input = soc_name_input_lower.rstrip('x')
                            base_soc = soc_name_lower.rstrip('x')

                            if (base_input.startswith(base_soc) or
                                base_soc.startswith(base_input) or
                                base_input in base_soc or
                                base_soc in base_input):
                                partial_match = (family_name, series_name, soc_name)

    # 如果没有精确匹配，返回部分匹配
    return partial_match

def parse_soc_info(compatible_str, yaml_file_path, fuzzy_match=False):
    """
    解析SOC信息，支持模糊匹配并忽略大小写

    Args:
        compatible_str: 兼容性字符串
        yaml_file_path: YAML文件路径
        fuzzy_match: 是否启用模糊匹配

    Returns:
        tuple: (vendor, family, series, soc_name)
    """
    try:
        # 解析兼容性字符串
        parts = compatible_str.split(',')
        if len(parts) != 2:
            raise ValueError(f"Invalid compatible string format: {compatible_str}")

        vendor = parts[0].strip()
        soc_name_input = parts[1].strip()

        # 加载YAML文件
        with open(yaml_file_path, 'r') as file:
            data = yaml.safe_load(file)

        # 查找SOC信息
        if fuzzy_match:
            result = find_soc_in_yaml(soc_name_input, data)
        else:
            # 非模糊匹配也要忽略大小写
            result = None
            soc_name_input_lower = soc_name_input.lower()

            if 'family' in data:
                for family in data['family']:
                    family_name = family.get('name', '')
                    if 'series' in family:
                        for series in family['series']:
                            series_name = series.get('name', '')
                            if 'socs' in series:
                                for soc in series['socs']:
                                    soc_name = soc.get('name', '')
                                    soc_name_lower = soc_name.lower()
                                    if soc_name_lower == soc_name_input_lower:
                                        result = (family_name, series_name, soc_name)
                                        break
                                if result:
                                    break
                        if result:
                            break

        if result:
            family, series, exact_soc_name = result
            return vendor, family, series, exact_soc_name
        else:
            print(f"Error: SOC '{soc_name_input}' not found in YAML file", file=sys.stderr)
            return None

    except Exception as e:
        print(f"Error parsing SOC info: {e}", file=sys.stderr)
        return None


def dt_get_soc_compat_name(kconf, _):
    if doc_mode or edt is None:
        return "n"

    try:
        node = edt.get_node("/")
    except edtlib.EDTError:
        return "n"

    if "compatible" not in node.props:
        return "n"

    return str(node.props["compatible"].val[0])


def get_soc_vender(kconf, _, soc_compat_str):
    result = parse_soc_info(soc_compat_str, soc_yaml_file, True)
    if result:
        vendor, _, _, _ = result
        return vendor
    else:
        return None

def get_soc_family(kconf, _, soc_compat_str):
    result = parse_soc_info(soc_compat_str, soc_yaml_file, True)
    if result:
        _, family, _, _ = result
        return family
    else:
        return None

def get_soc_series(kconf, _, soc_compat_str):
    result = parse_soc_info(soc_compat_str, soc_yaml_file, True)
    if result:
        _, _, series, _ = result
        return series
    else:
        return None

def get_soc_name(kconf, _, soc_compat_str):
    result = parse_soc_info(soc_compat_str, soc_yaml_file, True)
    if result:
        _, _, _, soc_name = result
        return soc_name
    else:
        return None

# Keys in this dict are the function names as they appear
# in Kconfig files. The values are tuples in this form:
#
#       (python_function, minimum_number_of_args, maximum_number_of_args)
#
# Each python function is given a kconf object and its name in the
# Kconfig file, followed by arguments from the Kconfig file.
#
# See the kconfiglib documentation for more details.
functions = {
        "str_is_equal": (str_is_equal, 2, 2),
        "dt_get_soc_compat_name": (dt_get_soc_compat_name, 0, 1),
        "get_soc_name": (get_soc_name, 1, 1),
        "get_soc_series": (get_soc_series, 1, 1),
        "get_soc_family": (get_soc_family, 1, 1),
        "get_soc_vender": (get_soc_vender, 1, 1),
        "dt_has_compat": (dt_has_compat, 1, 1),
        "dt_compat_enabled": (dt_compat_enabled, 1, 1),
        "dt_compat_enabled_num": (dt_compat_enabled_num, 1, 1),
        "dt_compat_on_bus": (dt_compat_on_bus, 2, 2),
        "dt_compat_any_has_prop": (dt_compat_any_has_prop, 2, 3),
        "dt_compat_any_not_has_prop": (dt_compat_any_not_has_prop, 2, 2),
        "dt_chosen_label": (dt_chosen_label, 1, 1),
        "dt_chosen_enabled": (dt_chosen_enabled, 1, 1),
        "dt_chosen_path": (dt_chosen_path, 1, 1),
        "dt_chosen_has_compat": (dt_chosen_has_compat, 2, 2),
        "dt_path_enabled": (dt_node_enabled, 1, 1),
        "dt_alias_enabled": (dt_node_enabled, 1, 1),
        "dt_nodelabel_exists": (dt_nodelabel_exists, 1, 1),
        "dt_nodelabel_enabled": (dt_nodelabel_enabled, 1, 1),
        "dt_nodelabel_enabled_with_compat": (dt_nodelabel_enabled_with_compat, 2, 2),
        "dt_chosen_reg_addr_int": (dt_chosen_reg, 1, 3),
        "dt_chosen_reg_addr_hex": (dt_chosen_reg, 1, 3),
        "dt_chosen_reg_size_int": (dt_chosen_reg, 1, 3),
        "dt_chosen_reg_size_hex": (dt_chosen_reg, 1, 3),
        "dt_node_reg_addr_int": (dt_node_reg, 1, 3),
        "dt_node_reg_addr_hex": (dt_node_reg, 1, 3),
        "dt_node_reg_size_int": (dt_node_reg, 1, 3),
        "dt_node_reg_size_hex": (dt_node_reg, 1, 3),
        "dt_nodelabel_reg_addr_int": (dt_nodelabel_reg, 1, 3),
        "dt_nodelabel_reg_addr_hex": (dt_nodelabel_reg, 1, 3),
        "dt_nodelabel_reg_size_int": (dt_nodelabel_reg, 1, 3),
        "dt_nodelabel_reg_size_hex": (dt_nodelabel_reg, 1, 3),
        "dt_node_bool_prop": (dt_node_bool_prop, 2, 2),
        "dt_nodelabel_bool_prop": (dt_nodelabel_bool_prop, 2, 2),
        "dt_nodelabel_int_prop": (dt_nodelabel_int_prop, 2, 2),
        "dt_chosen_bool_prop": (dt_chosen_bool_prop, 2, 2),
        "dt_node_has_prop": (dt_node_has_prop, 2, 2),
        "dt_nodelabel_has_prop": (dt_nodelabel_has_prop, 2, 2),
        "dt_node_int_prop_int": (dt_node_int_prop, 2, 3),
        "dt_node_int_prop_hex": (dt_node_int_prop, 2, 3),
        "dt_node_array_prop_int": (dt_node_array_prop, 3, 4),
        "dt_node_array_prop_hex": (dt_node_array_prop, 3, 4),
        "dt_node_ph_array_prop_int": (dt_node_ph_array_prop, 4, 5),
        "dt_node_ph_array_prop_hex": (dt_node_ph_array_prop, 4, 5),
        "dt_node_ph_prop_path": (dt_node_ph_prop_path, 2, 2),
        "dt_node_str_prop_equals": (dt_node_str_prop_equals, 3, 3),
        "dt_nodelabel_has_compat": (dt_nodelabel_has_compat, 2, 2),
        "dt_node_has_compat": (dt_node_has_compat, 2, 2),
        "dt_nodelabel_path": (dt_nodelabel_path, 1, 1),
        "dt_node_parent": (dt_node_parent, 1, 1),
        "dt_nodelabel_array_prop_has_val": (dt_nodelabel_array_prop_has_val, 3, 3),
        "dt_gpio_hogs_enabled": (dt_gpio_hogs_enabled, 0, 0),
        "dt_chosen_partition_addr_int": (dt_chosen_partition_addr, 1, 3),
        "dt_chosen_partition_addr_hex": (dt_chosen_partition_addr, 1, 3),
        "normalize_upper": (normalize_upper, 1, 1),
        "shields_list_contains": (shields_list_contains, 1, 1),
        "substring": (substring, 2, 3),
        "add": (arith, 1, 255),
        "add_hex": (arith, 1, 255),
        "sub": (arith, 1, 255),
        "sub_hex": (arith, 1, 255),
        "mul": (arith, 1, 255),
        "mul_hex": (arith, 1, 255),
        "div": (arith, 1, 255),
        "div_hex": (arith, 1, 255),
        "mod": (arith, 1, 255),
        "mod_hex": (arith, 1, 255),
        "max": (arith, 1, 255),
        "max_hex": (arith, 1, 255),
        "min": (arith, 1, 255),
        "min_hex": (arith, 1, 255),
        "inc": (inc_dec, 1, 255),
        "inc_hex": (inc_dec, 1, 255),
        "dec": (inc_dec, 1, 255),
        "dec_hex": (inc_dec, 1, 255),
}
