#!/usr/bin/env python3
"""
DeviceTrait 特化生成器 — YAML 驱动版。

从 DT 绑定 YAML 的 cxx-driver 节读取驱动映射，
解析 devicetree_generated.h，为匹配的设备树节点生成 C++ 模板特化。

加新驱动只需：
  1. 写 .h + .cc
  2. 在 binding YAML 的 child-binding（或节点自身）添加 cxx-driver 节

用法:
    python gen_device_traits.py <devicetree_generated.h> <output.h> <bindings_dir>
"""

import os
import re
import sys
import yaml


# ─── 解析 binding YAML 的 cxx-driver 节 ────────────────────────────────

def parse_yaml_comments(filepath):
    """解析 binding YAML 的 cxx-driver 节。

    YAML 格式:
        compatible: "gpio-leds"
        cxx-driver:
          template: GpioPort
          header: drivers/gpio.h
          args:
            - phandle-reg: gpios
            - field: gpios/pin
            - field: gpios/flags
    """
    with open(filepath, 'r', encoding='utf-8') as f:
        data = yaml.safe_load(f)

    if not isinstance(data, dict):
        return None, None

    compatible = data.get('compatible')
    cxx_raw = data.get('cxx-driver')

    if not compatible or not isinstance(cxx_raw, dict):
        return None, None

    template = cxx_raw.get('template')
    header = cxx_raw.get('header')
    raw_args = cxx_raw.get('args', [])

    if not template or not header:
        return None, None

    args = []
    for item in raw_args:
        if isinstance(item, str):
            # 无值参数：node-reg, parent-reg, irq, opt_irq
            if item == 'node-reg':
                args.append(('node_reg', None))
            elif item == 'parent-reg':
                args.append(('parent_reg', None))
            elif item == 'irq':
                args.append(('irq', None))
            elif item == 'opt_irq':
                args.append(('opt_irq', None))
        elif isinstance(item, dict) and len(item) == 1:
            key, val = next(iter(item.items()))
            if key == 'phandle-reg':
                args.append(('phandle_reg', val))
            elif key == 'node-reg':
                args.append(('node_reg', None))
            elif key == 'parent-reg':
                args.append(('parent_reg', None))
            elif key == 'field':
                parts = val.split('/')
                if len(parts) == 2:
                    args.append(('prop_field_val', parts[0], parts[1]))
            elif key == 'prop':
                args.append(('prop_val', val))
            elif key == 'literal':
                args.append(('literal', val))

    cxx = {'template': template, 'header': header, 'args': args}
    return compatible, cxx


def normalize_compat(s):
    """compatible 字符串标准化：连字符/点/逗号 → 下划线。"""
    return s.replace('-', '_').replace('.', '_').replace(',', '_')


def scan_bindings(bindings_dir):
    """扫描所有 binding YAML，提取 compatible → cxx-driver 映射。"""
    compat_map = {}  # normalized_compatible -> { template, header, args }

    if not os.path.isdir(bindings_dir):
        return compat_map

    for root, dirs, files in os.walk(bindings_dir):
        for fname in sorted(files):
            if not fname.endswith('.yaml'):
                continue
            fpath = os.path.join(root, fname)
            compatible, cxx = parse_yaml_comments(fpath)
            if compatible and cxx and 'template' in cxx and 'args' in cxx:
                cxx['compatible'] = compatible  # 保留原始格式用于显示
                compat_map[normalize_compat(compatible)] = cxx

    return compat_map


# ─── 解析 devicetree_generated.h ───────────────────────────────────────

def parse_generated_header(filepath):
    """解析 devicetree_generated.h，提取所有节点信息。"""
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # ordinal
    ord_re = re.compile(r'^#define\s+(DT_N_(?:S_\w+)+)_ORD\s+(\d+)', re.MULTILINE)
    ordinals = {}
    for m in ord_re.finditer(content):
        ordinals[m.group(1)] = int(m.group(2))

    # reg address: node_REG_IDX_0_VAL_ADDRESS <number>
    reg_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_REG_IDX_0_VAL_ADDRESS\s+(\d+)',
        re.MULTILINE)
    reg_addrs = {}
    for m in reg_re.finditer(content):
        reg_addrs[m.group(1)] = int(m.group(2))

    # compatible 匹配: node_COMPAT_MATCHES_xxx 1
    compat_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_COMPAT_MATCHES_(\S+)\s+1',
        re.MULTILINE)
    node_compats = {}  # node_id -> [compat1, compat2, ...]
    for m in compat_re.finditer(content):
        node_compats.setdefault(m.group(1), []).append(m.group(2))

    # 节点拥有的属性: node_P_prop_EXISTS 1
    prop_exists_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_P_(\w+)_EXISTS\s+1', re.MULTILINE)
    node_props = {}
    for m in prop_exists_re.finditer(content):
        node_props.setdefault(m.group(1), set()).add(m.group(2))

    # phandle 引用: node_P_prop_IDX_0_PH target
    ph_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_P_(\w+)_IDX_0_PH\s+(DT_N_(?:S_\w+)+)',
        re.MULTILINE)
    phandles = {}
    for m in ph_re.finditer(content):
        phandles[(m.group(1), m.group(2))] = m.group(3)

    # 属性字段值: node_P_prop_IDX_0_VAL_field <number>
    val_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_P_(\w+)_IDX_\d+_VAL_(\w+)\s+(\d+)',
        re.MULTILINE)
    prop_fields = {}
    for m in val_re.finditer(content):
        prop_fields[(m.group(1), m.group(2), m.group(3))] = int(m.group(4))

    # 直接整数属性
    direct_props = {}
    for line in content.splitlines():
        m = re.match(r'^#define\s+(DT_N_(?:S_\w+)+)_P_(\w+)\s+(\d+)\s*$', line)
        if not m:
            continue
        prop = m.group(2)
        if any(prop.endswith(s) for s in ('_EXISTS', '_LEN', '_NUM')):
            continue
        if any(f'_{p}_' in prop for p in ('IDX', 'FOREACH', 'ENUM', 'PHA')):
            continue
        direct_props[(m.group(1), prop)] = int(m.group(3))

    # alias
    alias_re = re.compile(
        r'^#define\s+DT_N_ALIAS_(\w+)\s+(DT_N_(?:S_\w+)+)', re.MULTILINE)
    aliases = {}
    for m in alias_re.finditer(content):
        aliases[m.group(2)] = m.group(1)

    # IRQ numbers: node_IRQ_IDX_0_VAL_irq <number>
    irq_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_IRQ_IDX_\d+_VAL_irq\s+(\d+)',
        re.MULTILINE)
    irq_nums = {}
    for m in irq_re.finditer(content):
        irq_nums[m.group(1)] = int(m.group(2))

    # 节点 status 属性: node_P_status "okay" / "disabled"
    status_re = re.compile(
        r'^#define\s+(DT_N_(?:S_\w+)+)_P_status\s+"(\w+)"', re.MULTILINE)
    node_status = {}
    for m in status_re.finditer(content):
        node_status[m.group(1)] = m.group(2)

    # 构建父节点 → compatible 映射
    # 父节点 ID 是子节点 ID 的前缀（去掉最后一段 _S_xxx）
    parent_compat = {}  # node_id -> compatible (从父节点继承)
    for node_id, compats in node_compats.items():
        # 这个节点自己有 compatible
        for other_id in ordinals:
            if other_id != node_id and other_id.startswith(node_id + '_S_'):
                # other_id 是 node_id 的子节点
                if compats:
                    parent_compat[other_id] = compats[0]

    return {
        'ordinals': ordinals,
        'reg_addrs': reg_addrs,
        'node_compats': node_compats,
        'node_props': node_props,
        'phandles': phandles,
        'prop_fields': prop_fields,
        'direct_props': direct_props,
        'aliases': aliases,
        'parent_compat': parent_compat,
        'node_status': node_status,
        'irq_nums': irq_nums,
    }


# ─── 生成特化代码 ─────────────────────────────────────────────────────

def resolve_arg(arg, node_id, dt_data):
    """将参数模板解析为 C++ 源码片段。"""
    arg_type = arg[0]

    if arg_type == 'phandle_reg':
        prop = arg[1]
        target = dt_data['phandles'].get((node_id, prop))
        if target and target in dt_data['reg_addrs']:
            return f'DT_REG_ADDR({target})'
        return None

    elif arg_type == 'node_reg':
        if node_id in dt_data['reg_addrs']:
            return f'DT_REG_ADDR({node_id})'
        return None

    elif arg_type == 'parent_reg':
        # 从子节点 ID 推导父节点 ID（去掉最后一段 _S_xxx）
        last_sep = node_id.rfind('_S_')
        if last_sep > 0:
            parent_id = node_id[:last_sep]
            if parent_id in dt_data['reg_addrs']:
                return f'DT_REG_ADDR({parent_id})'
        return None

    elif arg_type == 'prop_field_val':
        prop, field = arg[1], arg[2]
        if (node_id, prop, field) in dt_data['prop_fields']:
            return f'{node_id}_P_{prop}_IDX_0_VAL_{field}'
        return None

    elif arg_type == 'prop_val':
        prop = arg[1]
        if (node_id, prop) in dt_data['direct_props']:
            return f'{node_id}_P_{prop}'
        if (node_id, prop, prop) in dt_data['prop_fields']:
            return f'{node_id}_P_{prop}_VAL_{prop}'
        return None

    elif arg_type == 'literal':
        return f'"{arg[1]}"'

    elif arg_type == 'irq':
        irq = dt_data['irq_nums'].get(node_id)
        if irq is not None:
            return str(irq)
        return None

    elif arg_type == 'opt_irq':
        irq = dt_data['irq_nums'].get(node_id)
        return str(irq) if irq is not None else '-1'

    return None


def get_node_compatible(node_id, dt_data):
    """获取节点的 compatible（自身或从父节点继承）。"""
    # 自身有 compatible
    compats = dt_data['node_compats'].get(node_id, [])
    if compats:
        return compats[0]
    # 从父节点继承
    return dt_data['parent_compat'].get(node_id)


def is_node_enabled(node_id, dt_data):
    """节点是否启用（status = "okay" 或无 status 属性）。"""
    status = dt_data['node_status'].get(node_id)
    if status is None:
        return True  # 无 status 属性，默认启用
    return status == 'okay'


def generate_specializations(dt_data, compat_map):
    """为所有匹配且启用的节点生成特化代码。"""
    specs = []
    used_headers = set()

    for node_id, ord_val in dt_data['ordinals'].items():
        compat = get_node_compatible(node_id, dt_data)
        if not compat or compat not in compat_map:
            continue

        enabled = is_node_enabled(node_id, dt_data)

        driver = compat_map[compat]
        args_code = []
        skip = False

        for arg in driver['args']:
            resolved = resolve_arg(arg, node_id, dt_data)
            if resolved is None:
                skip = True
                break
            args_code.append(resolved)

        if skip:
            continue

        alias = dt_data['aliases'].get(node_id, '')
        specs.append({
            'ord': ord_val,
            'alias': alias,
            'type': driver['template'],
            'args': args_code,
            'header': driver['header'],
            'enabled': enabled,
            'node_id': node_id,
        })
        used_headers.add(driver['header'])

    specs.sort(key=lambda s: s['ord'])
    return specs, used_headers


def write_output(specs, used_headers, output_path):
    """生成 drivers_generated.h。"""
    lines = [
        '// Auto-generated by gen_device_traits.py. DO NOT EDIT.',
        '#pragma once',
        '',
        '#include <device.h>',
    ]

    for header in sorted(used_headers):
        lines.append(f'#include <{header}>')

    lines.extend([
        '',
        '#pragma GCC diagnostic push',
        '#pragma GCC diagnostic ignored "-Wtemplate-body"',
        '',
        'namespace hal {',
        '',
    ])

    # DeviceTrait 特化 + 模板实例化（仅启用的节点）
    for spec in specs:
        if not spec['enabled']:
            continue

        ord_val = spec['ord']
        alias = spec['alias']
        template_type = spec['type']
        args = ',\n        '.join(spec['args'])

        comment = f'// {alias}' if alias else f'// ord={ord_val}'
        lines.append(f'{comment}')
        lines.append(f'template <> struct DeviceTrait<{ord_val}> {{')
        lines.append(f'    using type = {template_type}<')
        lines.append(f'        {args}>;')
        lines.append(f'}};')
        lines.append('')

    # 模板显式实例化（仅启用的节点）
    instantiations = []
    for spec in specs:
        if not spec['enabled']:
            continue
        template_type = spec['type']
        args = ', '.join(spec['args'])
        instantiations.append(f'template class {template_type}<{args}>;')

    if instantiations:
        lines.append('// 模板显式实例化（仅 DTS 中 status = "okay" 的节点）')
        lines.extend(instantiations)
        lines.append('')

    lines.extend([
        '} // namespace hal',
        '',
        '#pragma GCC diagnostic pop',
        '',
    ])

    # DT_INST_* 宏（供 .cc 中条件编译使用）
    # 格式：DT_INST_<TYPE>_<HEX_ADDR> — 按驱动类型 + 基地址索引
    lines.append('// 实例化标记宏 — 供驱动 .cc 中条件编译使用')
    for spec in specs:
        if not spec['enabled']:
            continue
        # 从 node_id 末尾提取基地址（如 DT_N_S_soc_S_serial_40011000 → 40011000）
        node_id = spec['node_id']
        addr_hex = node_id.split('_')[-1].upper()
        lines.append(f'#define DT_INST_{spec["type"]}_{addr_hex}')
    lines.append('')

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write('\n'.join(lines))


# ─── 入口 ─────────────────────────────────────────────────────────────

def main():
    if len(sys.argv) < 3:
        print(f'用法: {sys.argv[0]} <devicetree_generated.h> <output.h> [bindings_dir]')
        sys.exit(1)

    dt_header = sys.argv[1]
    output_path = sys.argv[2]
    bindings_dir = sys.argv[3] if len(sys.argv) > 3 else None

    # 1. 扫描 binding YAML
    compat_map = {}
    if bindings_dir:
        compat_map = scan_bindings(bindings_dir)
        if compat_map:
            print(f'扫描 binding YAML: {len(compat_map)} 个 cxx-driver 注册')
            for c, d in compat_map.items():
                print(f'  "{d.get("compatible", c)}" -> {d["template"]}')

    # 2. 解析设备树头文件
    dt_data = parse_generated_header(dt_header)

    # 3. 生成特化
    specs, used_headers = generate_specializations(dt_data, compat_map)

    # 4. 写入输出
    write_output(specs, used_headers, output_path)
    print(f'生成 {output_path}: {len(specs)} 个设备特化')


if __name__ == '__main__':
    main()
